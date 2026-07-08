#pragma once

#include <JuceHeader.h>

namespace play
{

//==============================================================================
/**
    The final node before the audio output: a brickwall safety limiter.

    A smoothed peak limiter with an absolute hard-clip ceiling, so a misbehaving
    plugin (runaway feedback, a badly mapped gain) can't send an unbounded signal
    to the PA. It stays in place regardless of the master bypass — the clickless,
    latency-compensated bypass itself lives in each PluginNode wrapper.
*/
class MasterProcessor : public juce::AudioProcessor
{
public:
    MasterProcessor()
        : juce::AudioProcessor (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    void setLimiterEnabled (bool shouldLimit) noexcept { limiterOn.store (shouldLimit); }
    bool isLimiterEnabled() const noexcept             { return limiterOn.load(); }

    //==============================================================================
    void prepareToPlay (double sampleRate, int) override
    {
        const auto rate = juce::jmax (1.0, sampleRate);
        releaseCoeff    = (float) std::exp (-1.0 / (releaseSeconds * rate));
        gainReduction   = 1.0f;
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        auto* l = buffer.getWritePointer (0);
        auto* r = numChannels > 1 ? buffer.getWritePointer (1) : l;

        // Non-finite guard runs unconditionally — even with the limiter disabled.
        // A plugin feedback loop or divide-by-zero can emit Inf/NaN; without this,
        // Inf * gain and jlimit(NaN) both propagate NaN straight to the PA. This is
        // the actual "a misbehaving plugin can't send garbage to the speakers"
        // guarantee; the smoothed limiter below is the musical part.
        if (! limiterOn.load())
        {
            for (int n = 0; n < numSamples; ++n)
            {
                l[n] = std::isfinite (l[n]) ? l[n] : 0.0f;
                if (r != l)
                    r[n] = std::isfinite (r[n]) ? r[n] : 0.0f;
            }
            return;
        }

        float gain = gainReduction;

        for (int n = 0; n < numSamples; ++n)
        {
            // Flush non-finite samples to zero before any arithmetic, so Inf can't
            // become NaN (Inf * 0) and NaN can't slip through the jlimit ceiling.
            float sl = std::isfinite (l[n]) ? l[n] : 0.0f;
            float sr = std::isfinite (r[n]) ? r[n] : 0.0f;

            const float peak    = juce::jmax (std::abs (sl), std::abs (sr));
            const float desired = peak > ceilingLinear ? ceilingLinear / peak : 1.0f;

            // Instant attack, smoothed release — stereo-linked so the image holds.
            if (desired < gain) gain = desired;
            else                gain = desired + (gain - desired) * releaseCoeff;

            // Absolute ceiling guarantees no sample past the limiter exceeds it.
            l[n] = juce::jlimit (-ceilingLinear, ceilingLinear, sl * gain);
            r[n] = juce::jlimit (-ceilingLinear, ceilingLinear, sr * gain);
        }

        gainReduction = gain;
    }

    //==============================================================================
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    const juce::String getName() const override       { return "Master"; }
    double getTailLengthSeconds() const override       { return 0.0; }
    bool acceptsMidi() const override                  { return false; }
    bool producesMidi() const override                 { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override                    { return false; }
    int getNumPrograms() override                      { return 1; }
    int getCurrentProgram() override                   { return 0; }
    void setCurrentProgram (int) override              {}
    const juce::String getProgramName (int) override   { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    static constexpr double releaseSeconds = 0.05;   // limiter release

    // Ceiling ~-0.5 dBFS — the hard limit that reaches the output device.
    const float ceilingLinear = juce::Decibels::decibelsToGain (-0.5f);

    std::atomic<bool> limiterOn { true };

    float releaseCoeff  = 0.999f;
    float gainReduction = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterProcessor)
};

} // namespace play

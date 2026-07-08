#pragma once

#include <JuceHeader.h>

namespace play
{

//==============================================================================
/**
    Hosts one plugin inside the graph and gives it a seamless bypass.

    Instead of the graph's instant on/off (which clicks, and which for a
    high-latency plugin only stays time-aligned because the graph reserves the
    latency), this wrapper:

      • always runs the plugin, and keeps a copy of the input delayed by exactly
        the plugin's latency, so the "dry" signal is time-aligned with the
        processed "wet" signal;
      • crossfades (~30 ms) between wet and dry when bypass toggles, so turning a
        plugin on or off never clicks — even with linear-phase / lookahead
        plugins that report a large latency;
      • reports the plugin's latency as its own, so the graph's overall latency
        compensation and reported output latency stay correct.

    Two independent bypass sources are OR-combined: the per-slot user toggle and
    the global master kill. The plugin keeps running while bypassed, so its tail
    and internal state are live the instant it re-engages.
*/
class PluginNode : public juce::AudioProcessor
{
public:
    explicit PluginNode (std::unique_ptr<juce::AudioPluginInstance> pluginToWrap)
        : juce::AudioProcessor (busesFor (*pluginToWrap)),
          inner (std::move (pluginToWrap))
    {
    }

    juce::AudioPluginInstance& getInner() noexcept { return *inner; }

    //==============================================================================
    void setUserBypass (bool shouldBypass) noexcept   { userBypass = shouldBypass;   updateTarget(); }
    void setMasterBypass (bool shouldBypass) noexcept { masterBypass = shouldBypass; updateTarget(); }
    bool isUserBypassed() const noexcept              { return userBypass; }

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        inner->setProcessingPrecision (juce::AudioProcessor::singlePrecision);
        inner->setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);
        inner->prepareToPlay (sampleRate, samplesPerBlock);

        const int latency = juce::jmax (0, inner->getLatencySamples());
        setLatencySamples (latency);

        const int numChannels = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels(), 1);

        dryBuffer.setSize (numChannels, samplesPerBlock, false, false, true);
        dryBuffer.clear();

        delayLength = latency;
        ring.setSize (numChannels, juce::jmax (1, latency), false, false, true);
        ring.clear();
        writePos = 0;

        mixRamp.resize ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);

        crossfadeStep = (float) (1.0 / (crossfadeSeconds * juce::jmax (1.0, sampleRate)));
        mix = target;   // snap to the current bypass state on (re)prepare
    }

    void releaseResources() override
    {
        inner->releaseResources();
        ring.setSize (0, 0);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const int numSamples = buffer.getNumSamples();
        const int numOut      = getTotalNumOutputChannels();
        const int numIn       = getTotalNumInputChannels();

        // Snapshot the dry input (aligned to the wet output by delaying it the
        // plugin's latency) before the plugin overwrites the buffer.
        for (int ch = 0; ch < numOut; ++ch)
        {
            const int srcCh = juce::jmin (ch, numIn - 1);

            if (srcCh >= 0)
                dryBuffer.copyFrom (ch, 0, buffer, srcCh, 0, numSamples);
            else
                dryBuffer.clear (ch, 0, numSamples);
        }

        applyDryDelay (numOut, numSamples);

        // Wet: run the plugin in place.
        inner->processBlock (buffer, midi);

        // Build the per-sample wet<->dry ramp once, then apply it to every channel.
        const float tgt = target;
        float m = mix;

        for (int n = 0; n < numSamples; ++n)
        {
            if (m < tgt)      m = juce::jmin (tgt, m + crossfadeStep);
            else if (m > tgt) m = juce::jmax (tgt, m - crossfadeStep);
            mixRamp[(size_t) n] = m;
        }

        mix = m;

        for (int ch = 0; ch < numOut; ++ch)
        {
            auto* wet = buffer.getWritePointer (ch);
            const auto* dry = dryBuffer.getReadPointer (ch);

            for (int n = 0; n < numSamples; ++n)
            {
                const float g = mixRamp[(size_t) n];
                wet[n] = wet[n] * (1.0f - g) + dry[n] * g;
            }
        }
    }

    //==============================================================================
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return inner->checkBusesLayoutSupported (layouts);
    }

    void reset() override                                   { inner->reset(); }
    void setNonRealtime (bool isNonRealtime) noexcept override { inner->setNonRealtime (isNonRealtime); }

    const juce::String getName() const override            { return inner->getName(); }
    double getTailLengthSeconds() const override           { return inner->getTailLengthSeconds(); }
    bool acceptsMidi() const override                      { return inner->acceptsMidi(); }
    bool producesMidi() const override                     { return inner->producesMidi(); }
    bool supportsDoublePrecisionProcessing() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override    { return nullptr; }
    bool hasEditor() const override                        { return false; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // State is the inner plugin's; the wrapper adds no persisted parameters.
    void getStateInformation (juce::MemoryBlock& data) override        { inner->getStateInformation (data); }
    void setStateInformation (const void* data, int size) override     { inner->setStateInformation (data, size); }

private:
    //==============================================================================
    static BusesProperties busesFor (juce::AudioPluginInstance& plugin)
    {
        BusesProperties props;

        for (int i = 0; i < plugin.getBusCount (true); ++i)
            if (auto* bus = plugin.getBus (true, i))
                props.addBus (true, bus->getName(), bus->getCurrentLayout(), bus->isEnabled());

        for (int i = 0; i < plugin.getBusCount (false); ++i)
            if (auto* bus = plugin.getBus (false, i))
                props.addBus (false, bus->getName(), bus->getCurrentLayout(), bus->isEnabled());

        return props;
    }

    void updateTarget() noexcept { target = (userBypass || masterBypass) ? 1.0f : 0.0f; }

    /** Delays dryBuffer in place by the plugin's latency, so the dry copy lines
        up sample-for-sample with the processed output. */
    void applyDryDelay (int numChannels, int numSamples)
    {
        if (delayLength <= 0)
            return;

        int endPos = writePos;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* x = dryBuffer.getWritePointer (ch);
            auto* r = ring.getWritePointer (juce::jmin (ch, ring.getNumChannels() - 1));

            int idx = writePos;

            for (int n = 0; n < numSamples; ++n)
            {
                const float in = x[n];
                x[n]    = r[idx];   // read the delayed sample
                r[idx]  = in;       // store the current one
                if (++idx >= delayLength)
                    idx = 0;
            }

            endPos = idx;
        }

        writePos = endPos;
    }

    static constexpr double crossfadeSeconds = 0.03;

    std::unique_ptr<juce::AudioPluginInstance> inner;

    std::atomic<bool> userBypass   { false };
    std::atomic<bool> masterBypass { false };
    std::atomic<float> target      { 0.0f };   // 0 = wet, 1 = dry

    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> ring;
    std::vector<float>       mixRamp;
    int   delayLength   = 0;
    int   writePos      = 0;
    float crossfadeStep = 0.001f;
    float mix           = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginNode)
};

} // namespace play

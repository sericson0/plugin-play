#pragma once

#include <JuceHeader.h>

namespace play
{

//==============================================================================
/**
    An optional second output path that mirrors the main output — e.g. a DAC's
    headphone endpoint or booth monitors alongside the main speakers.

    JUCE's AudioDeviceManager drives exactly one device, so this owns its own
    output-only AudioIODevice, always opened on shared-mode WASAPI so it can run
    alongside ASIO or exclusive-mode mains (a second ASIO device usually can't
    be opened at all, and exclusive mode would steal the endpoint).

    The main device callback pushes its finished output into a lock-free FIFO
    (push()); the monitor device's callback pulls it back out through a
    Lagrange resampler. The two devices run on independent clocks, so the
    resampling ratio is continuously nudged (a fraction of a percent) to hold
    the FIFO at a fixed target depth — without this, any clock drift would
    slowly starve or overflow the FIFO and click every few minutes. The same
    resampler also absorbs an outright sample-rate mismatch between the two
    devices.

    On start (and after an underrun, e.g. the mains device being switched) the
    monitor plays silence while the FIFO refills to the target depth, then
    fades back in. The monitor path therefore runs ~20-40 ms behind the mains:
    fine for headphones or a booth, audible as a slap echo if both outputs
    fill the same room.

    Threading: open()/close() on the message thread only. push() is called
    from the main device's audio thread and the render callback runs on the
    monitor device's audio thread; they share only the AbstractFifo and a few
    atomics, and the FIFO storage is allocated once up front, so neither ever
    locks or allocates.
*/
class MonitorOutput : private juce::AudioIODeviceCallback
{
public:
    MonitorOutput();
    ~MonitorOutput() override;

    /** Output device names the monitor can open (the shared WASAPI type's list). */
    static juce::StringArray availableDevices (juce::AudioDeviceManager&);

    /** Opens and starts the named output device, playing to the stereo pair
        beginning at startChannel (0 = "1+2", 2 = "3+4", ...; out-of-range falls
        back to the first pair). Returns "" on success, else a human-readable
        error. Any previously open monitor device is closed first. */
    juce::String open (juce::AudioDeviceManager&, const juce::String& deviceName,
                       int startChannel = 0);

    void close();
    bool isRunning() const;

    /** Total output channels of the open device (0 while closed) — lets the UI
        offer its channel pairs. */
    int outputChannelCount() const;

    //==============================================================================
    /** Called from the MAIN device's audio callback with its finished output.
        Lock-free; drops samples if the FIFO is full (only happens while the
        monitor isn't draining). Mono input is duplicated to both channels. */
    void push (const float* const* channels, int numChannels, int numSamples) noexcept;

    /** The main device's format, so the resampler knows the rate of the pushed
        samples. Called from audioDeviceAboutToStart of the main device. */
    void setSourceFormat (double sampleRate, int blockSize) noexcept;

private:
    //==============================================================================
    void audioDeviceIOCallbackWithContext (const float* const* input, int numInputChannels,
                                           float* const* output, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    /** FIFO depth (in source-rate samples) the drift control steers towards. */
    int targetFillSamples() const noexcept;

    static juce::AudioIODeviceType* findMonitorType (juce::AudioDeviceManager&);

    //==============================================================================
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String currentName;

    // Fixed-size FIFO between the two audio threads; allocated once, never
    // resized (capacity is ~0.7 s at 96 kHz — far above any target depth).
    static constexpr int fifoCapacity = 65536;
    juce::AbstractFifo fifo { fifoCapacity };
    juce::AudioBuffer<float> fifoBuffer;

    // Written by the message thread / main audio thread, read by the monitor
    // audio thread every block.
    std::atomic<bool>   active { false };       // gates push(); false while closed
    std::atomic<double> sourceRate  { 0.0 };
    std::atomic<int>    sourceBlock { 0 };

    // Everything below is touched only on the monitor device's audio thread
    // (or before start).
    double monitorRate = 0.0;
    int    monitorBlock = 0;
    juce::LagrangeInterpolator resamplers[2];
    juce::AudioBuffer<float> scratch;           // contiguous resampler input
    juce::AudioBuffer<float> discard;           // sink for unused channel output

    bool   priming = true;                      // refilling to target; output silence
    bool   drainPending = false;                // flush stale FIFO content on start
    double smoothedFill = 0.0;
    float  fadeGain = 0.0f;                     // click-free ramp out of priming

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonitorOutput)
};

} // namespace play

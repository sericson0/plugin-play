#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <thread>

namespace play
{

//==============================================================================
/** A selectable audio source: an app with an audio session on the default render
    endpoint, or an open app that could start playing (the DJ app the user points
    us at). */
struct AudioSource
{
    juce::uint32 pid = 0;
    juce::String executable;   // e.g. "virtualdj.exe"
    juce::String displayName;  // session name, or the window title for idle apps
    bool active = false;       // currently producing audio (vs. idle)
};

/** Enumerates selectable sources: apps with an audio session on the default render
    endpoint (system sounds skipped), plus open apps that have a window but no
    session yet (e.g. a music app that hasn't pressed play) so idle apps are still
    pickable. Message-thread only (uses COM); returns an empty list on non-Windows
    or if enumeration fails. */
std::vector<AudioSource> enumerateAudioSources();

//==============================================================================
/**
    Driverless capture of one process's audio via
    AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK (Windows 10 2004+), together with
    the endpoint-master-mute that silences the dry signal at the speakers while
    the capture keeps flowing. Both behaviours were validated by-ear in
    experiments/process-loopback/ (session mute kills the capture; endpoint master
    mute does not).

    A background thread pulls captured packets into a lock-free FIFO; the audio
    thread drains it via read(). The capture format is dictated to match the
    output device's sample rate, so Windows performs any rate conversion of the
    source and the FIFO only has to absorb drift between the capture and output
    clocks (plus a small safety buffer).
*/
class LoopbackCapture
{
public:
    LoopbackCapture();
    ~LoopbackCapture();

    /** Starts capturing targetPid at the given stereo sample rate and mutes the
        default render endpoint (remembering its previous state). Retargeting is a
        stop() + start(). Returns false if process loopback is unavailable
        (pre-2004 Windows) or activation failed. */
    bool start (juce::uint32 targetPid, double sampleRate);

    /** Stops capture and restores the endpoint mute to what it was before. */
    void stop();

    bool isActive() const noexcept          { return active.load(); }
    juce::uint32 targetPid() const noexcept { return pid.load(); }

    /** Audio-thread, lock-free, no allocation: fills dest with up to numSamples
        stereo frames from the FIFO, zero-filling any shortfall (underrun). */
    void read (float* const* dest, int numChannels, int numSamples) noexcept;

private:
   #if JUCE_WINDOWS
    void captureThreadEntry();
   #endif

    juce::AbstractFifo fifo { 1 };
    juce::AudioBuffer<float> ring;       // deinterleaved stereo, fifo-sized
    std::atomic<bool> active { false };
    std::atomic<bool> stopFlag { false };
    std::atomic<juce::uint32> pid { 0 };
    std::thread captureThread;
    double captureRate = 48000.0;

    // Endpoint mute state to restore on stop(); -1 = we didn't mute.
    int previousEndpointMute = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopbackCapture)
};

} // namespace play

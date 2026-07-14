#pragma once

#include <JuceHeader.h>

#if JUCE_MAC

// AudioSource (and the enumerateAudioSources declaration) are shared with the
// Windows backend; the macOS implementation of both lives in ProcessTap.mm.
#include "LoopbackCapture.h"

#include <memory>

namespace play
{

//==============================================================================
/**
    Driverless capture of one app's audio via a Core Audio process tap
    (macOS 14.4+): a CATapDescription / AudioHardwareCreateProcessTap tap wrapped
    in a private aggregate device that we read with an IOProc.

    The tap is created with muteBehavior = muted-when-tapped, so the tapped app's
    own speaker output goes silent while the capture keeps flowing — the dry/wet
    separation that needed the app-to-cable redirect on Windows. Output can go to
    ANY device, including the same speakers the app was using. Everything dies
    with the process (no persistent OS routing state), so there is no marker-file
    crash recovery to do on this platform.

    Mirrors LoopbackCapture's interface so AudioEngine can use either as its
    app-capture backend (the AppCapture alias): a background IOProc fills a
    lock-free FIFO; the audio thread drains it via read(), which also resamples
    when the tap's stream rate differs from the engine's output rate.

    The first tap creation triggers the macOS "System Audio Recording" permission
    prompt (NSAudioCaptureUsageDescription in the plist); start() returns false
    if it is denied.

    Defensive wrinkle: long-running taps have been reported to decay into
    all-zero samples. A watchdog timer rebuilds the tap after sustained exact
    silence, with backoff so a legitimately paused source doesn't cause a
    rebuild storm.
*/
class ProcessTapCapture
{
public:
    ProcessTapCapture();
    ~ProcessTapCapture();

    /** Starts tapping targetPid, delivering stereo frames at the given engine
        sample rate (resampling internally if the tap runs at another rate).
        Retargeting is a stop() + start(). Returns false if the process has no
        audio presence, tap creation failed, or capture permission was denied. */
    bool start (juce::uint32 targetPid, double sampleRate);

    /** Destroys the tap and aggregate device (un-muting the tapped app). */
    void stop();

    bool isActive() const noexcept;
    juce::uint32 targetPid() const noexcept;

    /** Audio-thread, lock-free, no allocation: fills dest with up to numSamples
        stereo frames from the FIFO, zero-filling any shortfall (underrun). */
    void read (float* const* dest, int numChannels, int numSamples) noexcept;

    /** True if a process with this id is currently running. Used to detect the
        tapped app exiting so the UI can stop capture and tell the user. */
    static bool isProcessRunning (juce::uint32 pid);

private:
    // All Core Audio / Objective-C state lives in the implementation file; the
    // Impl exists from a successful start() until stop().
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessTapCapture)
};

} // namespace play

#endif // JUCE_MAC

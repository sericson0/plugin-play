// Compiled as Objective-C++ with ARC (see CMakeLists): NS objects release
// themselves; Core Foundation objects obtained from C APIs still need CFRelease.
//
// The Apple SDK headers MUST be imported before ProcessTap.h: the generated
// JuceHeader pulls `using namespace juce` into the global namespace, which makes
// Carbon's Point/Component types ambiguous inside any SDK header parsed after it.
// (This file is only ever compiled on macOS — see the if(APPLE) in CMakeLists.)
#import <AppKit/AppKit.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/CATapDescription.h>
#import <CoreAudio/AudioHardwareTapping.h>

#include "ProcessTap.h"

#if JUCE_MAC

#include <libproc.h>
#include <signal.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace play
{

//==============================================================================
// Small helpers over the (verbose) AudioObject property API.

static AudioObjectPropertyAddress propertyAddress (AudioObjectPropertySelector selector)
{
    return { selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
}

template <typename T>
static bool getProperty (AudioObjectID object, AudioObjectPropertySelector selector, T& out)
{
    auto address = propertyAddress (selector);
    UInt32 size = sizeof (T);
    return AudioObjectGetPropertyData (object, &address, 0, nullptr, &size, &out) == noErr;
}

static juce::String getStringProperty (AudioObjectID object, AudioObjectPropertySelector selector)
{
    CFStringRef value = nullptr;
    if (! getProperty (object, selector, value) || value == nullptr)
        return {};

    juce::String result = juce::String::fromCFString (value);
    CFRelease (value);
    return result;
}

/** The HAL's AudioObjectID for a process, or kAudioObjectUnknown if the process
    has no audio presence (never registered with the audio server). */
static AudioObjectID processObjectForPid (pid_t pid)
{
    auto address = propertyAddress (kAudioHardwarePropertyTranslatePIDToProcessObject);
    AudioObjectID object = kAudioObjectUnknown;
    UInt32 size = sizeof (object);

    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &address,
                                    sizeof (pid), &pid, &size, &object) != noErr)
        return kAudioObjectUnknown;

    return object;
}

//==============================================================================
/** Enumerates selectable sources: every process the audio server knows about
    that is either a real (dockable) app or currently producing output audio.
    This is the macOS counterpart of the WASAPI session walk on Windows; the
    `executable` field holds the bundle id (stable across relaunches, so saved
    sessions can re-resolve it to a live process, mirroring the exe-name logic). */
std::vector<AudioSource> enumerateAudioSources()
{
    std::vector<AudioSource> sources;

    // The process-object list and per-process properties are macOS 14.4+. On older
    // systems there is no app-capture list (the BlackHole device path is used
    // instead), so return empty and let the UI hide the "Capture an app" section.
    if (__builtin_available (macOS 14.4, *))
    {
    // Size then fetch the HAL's process object list.
    auto address = propertyAddress (kAudioHardwarePropertyProcessObjectList);
    UInt32 dataSize = 0;
    if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &address, 0, nullptr, &dataSize) != noErr
        || dataSize == 0)
        return sources;

    std::vector<AudioObjectID> objects (dataSize / sizeof (AudioObjectID));
    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &address, 0, nullptr,
                                    &dataSize, objects.data()) != noErr)
        return sources;
    objects.resize (dataSize / sizeof (AudioObjectID));

    const pid_t ourPid = getpid();

    for (auto object : objects)
    {
        pid_t pid = 0;
        if (! getProperty (object, kAudioProcessPropertyPID, pid) || pid <= 0 || pid == ourPid)
            continue;

        UInt32 runningOutput = 0;
        getProperty (object, kAudioProcessPropertyIsRunningOutput, runningOutput);

        const auto bundleID = getStringProperty (object, kAudioProcessPropertyBundleID);

        NSRunningApplication* app =
            [NSRunningApplication runningApplicationWithProcessIdentifier: pid];

        // Keep real apps (things with a UI presence) plus anything actively
        // playing (covers helper processes that do an app's audio for it). This
        // mirrors the Windows filter of visible-window apps + active sessions.
        const bool isRealApp = app != nil
                               && app.activationPolicy != NSApplicationActivationPolicyProhibited;
        if (! isRealApp && runningOutput == 0)
            continue;

        AudioSource source;
        source.pid    = (juce::uint32) pid;
        source.active = runningOutput != 0;

        if (app != nil && app.localizedName != nil)
            source.displayName = juce::String::fromCFString ((__bridge CFStringRef) app.localizedName);

        // Executable identity, most stable first: bundle id, else process name.
        if (bundleID.isNotEmpty())
            source.executable = bundleID;
        else
        {
            char name[2 * MAXCOMLEN] = {};
            if (proc_name (pid, name, sizeof (name)) > 0)
                source.executable = juce::String::fromUTF8 (name);
            else
                source.executable = "PID " + juce::String (pid);
        }

        if (source.displayName.isEmpty())
            source.displayName = source.executable.fromLastOccurrenceOf (".", false, false);

        // One entry per process id (the HAL shouldn't duplicate, but be safe).
        bool duplicate = false;
        for (auto& existing : sources)
            if (existing.pid == source.pid)
                { duplicate = true; break; }

        if (! duplicate)
            sources.push_back (std::move (source));
    }

    // Active (audible) sources first, then alphabetical — same order as Windows.
    std::sort (sources.begin(), sources.end(), [] (const AudioSource& a, const AudioSource& b)
    {
        if (a.active != b.active)
            return a.active;
        return a.displayName.compareIgnoreCase (b.displayName) < 0;
    });
    }  // __builtin_available (macOS 14.4)

    return sources;
}

//==============================================================================
// The Impl is allocated once, in the ProcessTapCapture constructor, and lives
// until its destructor — start()/stop() only reconfigure it. That gives the
// audio thread a stable pointer to dereference in read(); the `active` flag
// (not the pointer) says whether anything is flowing, exactly like the member
// layout of the Windows LoopbackCapture.
struct ProcessTapCapture::Impl : private juce::Timer
{
    Impl() = default;

    ~Impl() override
    {
        shutdown();
    }

    //==========================================================================
    // Message-thread lifecycle.

    bool start (juce::uint32 targetPid, double engineSampleRate)
    {
        shutdown();

        pid.store (targetPid);
        engineRate = engineSampleRate;

        // The tap must exist first: its stream format dictates the incoming rate,
        // which sets the resampler ratio. Only then may the IOProc start writing.
        if (! createTap())
        {
            pid.store (0);
            return false;
        }

        // Fixed maximum size so a re-start or a watchdog rebuild NEVER reallocates
        // the ring or FIFO under a concurrent audio-thread read() (setSize with
        // avoidReallocating is a no-op after the first allocation; the `active`
        // flag alone can't guard a reallocation). Ample stereo headroom to absorb
        // drift and scheduling jitter: ~10 s at 48 kHz, ~1.4 s at 384 kHz.
        ring.setSize (2, maxRingFrames, false, true, true);
        ring.clear();
        fifo.setTotalSize (maxRingFrames);
        fifo.reset();

        scratch.setSize (2, scratchCapacity, false, true, true);
        scratch.clear();
        scratchFill = 0;
        zeroFrames.store (0);
        lastCallbackMs.store (juce::Time::getMillisecondCounter());
        resamplerNeedsReset.store (true);
        rebuildAfterSeconds = 15.0;
        stallAfterSeconds   = 5.0;

        if (! createAggregateAndStart())
        {
            destroyCoreAudioObjects();
            pid.store (0);
            return false;
        }

        active.store (true, std::memory_order_release);
        startTimer (1000);   // silence watchdog (see timerCallback)
        return true;
    }

    void shutdown()
    {
        // Flip inactive first so a concurrent audio-thread read() emits silence;
        // the buffers themselves stay allocated for any read already in flight.
        active.store (false, std::memory_order_release);
        stopTimer();
        destroyCoreAudioObjects();
        pid.store (0);
    }

    // Message thread. Retune the resampler when the engine's output rate changes
    // (device/rate switch). Deliberately NOT a restart: start() would tear down
    // and recreate the Core Audio objects, REALLOCATE the ring/FIFO the audio
    // thread is reading, and needlessly re-mute the tapped app. The tap keeps its
    // own rate; the read() resampler already bridges tapRate -> engineRate, so a
    // lock-free ratio update is all that is required.
    void setEngineRate (double newRate) noexcept
    {
        engineRate = juce::jmax (1.0, newRate);
        ratio.store (tapRate / engineRate, std::memory_order_relaxed);
        resamplerNeedsReset.store (true, std::memory_order_release);
    }

    //==========================================================================
    bool createTap()
    {
        // The tap API is macOS 14.4+. This is belt-and-braces: start() already
        // refuses on older systems, but the guard also keeps the weak-linked
        // symbols from ever being called where they resolve to null.
        if (__builtin_available (macOS 14.4, *))
        {
            const auto processObject = processObjectForPid ((pid_t) pid.load());
            if (processObject == kAudioObjectUnknown)
                return failed ("process has no audio presence");

            // The tap: stereo mixdown of the one process, private (invisible to
            // other HAL clients), muting the app's own speaker output while tapped.
            // Creating it triggers the "System Audio Recording" TCC prompt on the
            // very first use; a denial makes AudioHardwareCreateProcessTap fail.
            CATapDescription* description =
                [[CATapDescription alloc] initStereoMixdownOfProcesses: @[ @(processObject) ]];
            description.name         = @"Plugin Play tap";
            description.privateTap   = YES;
            description.muteBehavior = CATapMutedWhenTapped;

            if (AudioHardwareCreateProcessTap (description, &tap) != noErr || tap == kAudioObjectUnknown)
            {
                tap = kAudioObjectUnknown;
                return failed ("tap creation failed (permission denied?)");
            }

            tapUID = [description.UUID UUIDString];

            // The tap's stream format tells us the rate the frames will arrive at.
            AudioStreamBasicDescription format {};
            if (getProperty (tap, kAudioTapPropertyFormat, format) && format.mSampleRate > 0)
                tapRate = format.mSampleRate;
            else
                tapRate = engineRate;

            ratio.store (tapRate / engineRate);
            return true;
        }

        return failed ("process taps require macOS 14.4");
    }

    bool createAggregateAndStart()
    {
      if (__builtin_available (macOS 14.4, *))
      {
        // Wrap the tap in a private aggregate device so a plain IOProc can pull
        // its frames. The current default output is included as the aggregate's
        // clock-master subdevice (the proven-stable composition); the tap gets
        // drift compensation against that clock. We render silence to the
        // output side, so the subdevice is unaffected.
        AudioObjectID defaultOutput = kAudioObjectUnknown;
        if (! getProperty (kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, defaultOutput)
            || defaultOutput == kAudioObjectUnknown)
            return failed ("no default output device");

        const auto outputUIDString = getStringProperty (defaultOutput, kAudioDevicePropertyDeviceUID);
        if (outputUIDString.isEmpty())
            return failed ("default output has no UID");

        NSString* outputUID = [NSString stringWithUTF8String: outputUIDString.toRawUTF8()];

        NSDictionary* aggregateDescription = @{
            @kAudioAggregateDeviceNameKey:          @"Plugin Play Tap",
            @kAudioAggregateDeviceUIDKey:           [[NSUUID UUID] UUIDString],
            @kAudioAggregateDeviceMainSubDeviceKey: outputUID,
            @kAudioAggregateDeviceIsPrivateKey:     @YES,
            @kAudioAggregateDeviceIsStackedKey:     @NO,
            @kAudioAggregateDeviceTapAutoStartKey:  @YES,
            @kAudioAggregateDeviceSubDeviceListKey: @[ @{ @kAudioSubDeviceUIDKey: outputUID } ],
            @kAudioAggregateDeviceTapListKey: @[ @{
                @kAudioSubTapUIDKey: tapUID,
                @kAudioSubTapDriftCompensationKey: @YES } ],
        };

        if (AudioHardwareCreateAggregateDevice ((__bridge CFDictionaryRef) aggregateDescription,
                                                &aggregate) != noErr
            || aggregate == kAudioObjectUnknown)
        {
            aggregate = kAudioObjectUnknown;
            return failed ("aggregate device creation failed");
        }

        if (AudioDeviceCreateIOProcID (aggregate, ioProc, this, &ioProcID) != noErr
            || ioProcID == nullptr)
            return failed ("IOProc creation failed");

        if (AudioDeviceStart (aggregate, ioProcID) != noErr)
            return failed ("device start failed");

        return true;
      }

      return failed ("process taps require macOS 14.4");
    }

    void destroyCoreAudioObjects()
    {
        if (aggregate != kAudioObjectUnknown && ioProcID != nullptr)
        {
            AudioDeviceStop (aggregate, ioProcID);
            AudioDeviceDestroyIOProcID (aggregate, ioProcID);   // returns after any in-flight callback
        }
        ioProcID = nullptr;

        if (aggregate != kAudioObjectUnknown)
        {
            AudioHardwareDestroyAggregateDevice (aggregate);
            aggregate = kAudioObjectUnknown;
        }

        if (tap != kAudioObjectUnknown)
        {
            if (__builtin_available (macOS 14.4, *))
                AudioHardwareDestroyProcessTap (tap);   // un-mutes the tapped app
            tap = kAudioObjectUnknown;
        }

        tapUID = nil;
    }

    bool failed (const char* why)
    {
        DBG ("ProcessTap: " << why);
        juce::ignoreUnused (why);
        return false;
    }

    //==========================================================================
    // IOProc: real-time HAL thread. Deinterleave the tap frames into the ring,
    // track exact-silence for the watchdog, and emit silence to the output side.
    static OSStatus ioProc (AudioObjectID, const AudioTimeStamp*,
                            const AudioBufferList* inInputData, const AudioTimeStamp*,
                            AudioBufferList* outOutputData, const AudioTimeStamp*,
                            void* clientData)
    {
        auto& self = *static_cast<Impl*> (clientData);

        // Liveness stamp for the watchdog: a stalled aggregate stops calling this
        // proc entirely, which the zero-frame counter alone can never notice.
        self.lastCallbackMs.store (juce::Time::getMillisecondCounter(),
                                   std::memory_order_relaxed);

        if (outOutputData != nullptr)
            for (UInt32 i = 0; i < outOutputData->mNumberBuffers; ++i)
                if (auto* data = outOutputData->mBuffers[i].mData)
                    std::memset (data, 0, outOutputData->mBuffers[i].mDataByteSize);

        if (inInputData == nullptr || inInputData->mNumberBuffers == 0)
            return noErr;

        // The tap is a stereo mixdown: either one interleaved 2-channel buffer
        // or two mono buffers, depending on how the HAL surfaces it.
        const auto* buffers       = inInputData->mBuffers;   // ::AudioBuffer, not juce::
        const UInt32 chansInFirst = buffers[0].mNumberChannels;
        const float* left  = nullptr;
        const float* right = nullptr;
        UInt32 frames      = 0;
        UInt32 stride      = 1;

        if (chansInFirst >= 2)
        {
            const auto* data = static_cast<const float*> (buffers[0].mData);
            frames = buffers[0].mDataByteSize / (UInt32) (sizeof (float) * chansInFirst);
            left   = data;
            right  = data + 1;
            stride = chansInFirst;
        }
        else if (chansInFirst == 1)
        {
            left   = static_cast<const float*> (buffers[0].mData);
            right  = inInputData->mNumberBuffers > 1
                       ? static_cast<const float*> (buffers[1].mData) : left;
            frames = buffers[0].mDataByteSize / (UInt32) sizeof (float);
        }

        if (left == nullptr || right == nullptr || frames == 0)
            return noErr;

        // Watchdog bookkeeping: count consecutive exactly-zero frames.
        float peak = 0.0f;
        for (UInt32 n = 0; n < frames; ++n)
            peak = std::fmax (peak, std::fmax (std::fabs (left[n * stride]),
                                               std::fabs (right[n * stride])));
        if (peak == 0.0f)
            self.zeroFrames.fetch_add ((juce::int64) frames, std::memory_order_relaxed);
        else
            self.zeroFrames.store (0, std::memory_order_relaxed);

        const int writable = juce::jmin ((int) frames, self.fifo.getFreeSpace());
        if (writable <= 0)
            return noErr;   // overrun: drop, the reader will catch up

        int start1, size1, start2, size2;
        self.fifo.prepareToWrite (writable, start1, size1, start2, size2);

        auto* l = self.ring.getWritePointer (0);
        auto* r = self.ring.getWritePointer (1);

        auto deinterleave = [&] (int ringStart, int sourceOffset, int count)
        {
            for (int n = 0; n < count; ++n)
            {
                const auto s = (UInt32) (sourceOffset + n) * stride;
                l[ringStart + n] = left[s];
                r[ringStart + n] = right[s];
            }
        };

        if (size1 > 0) deinterleave (start1, 0,     size1);
        if (size2 > 0) deinterleave (start2, size1, size2);

        self.fifo.finishedWrite (size1 + size2);
        return noErr;
    }

    //==========================================================================
    // Audio-thread read: drain the FIFO, resampling tapRate -> engineRate when
    // they differ. Lock-free, no allocation (scratch is preallocated).
    void read (float* const* dest, int numChannels, int numSamples) noexcept
    {
        auto zeroFill = [&] (int fromSample)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                if (dest[ch] != nullptr)
                    juce::FloatVectorOperations::clear (dest[ch] + fromSample,
                                                        numSamples - fromSample);
        };

        if (! active.load (std::memory_order_acquire) || ring.getNumChannels() < 2)
        {
            zeroFill (0);
            return;
        }

        const double currentRatio = ratio.load (std::memory_order_relaxed);

        if (resamplerNeedsReset.exchange (false, std::memory_order_acq_rel))
        {
            for (auto& interp : interpolators)
                interp.reset();
            scratchFill = 0;
        }

        // Fast path: rates match, plain FIFO drain (the common case — engine
        // output and the tapped app usually both run at the device rate).
        if (std::fabs (currentRatio - 1.0) < 1.0e-6)
        {
            const int toRead = juce::jmin (fifo.getNumReady(), numSamples);

            int start1, size1, start2, size2;
            fifo.prepareToRead (toRead, start1, size1, start2, size2);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (dest[ch] == nullptr)
                    continue;

                const auto* source = ring.getReadPointer (juce::jmin (ch, 1));
                if (size1 > 0) juce::FloatVectorOperations::copy (dest[ch],         source + start1, size1);
                if (size2 > 0) juce::FloatVectorOperations::copy (dest[ch] + size1, source + start2, size2);
            }

            fifo.finishedRead (size1 + size2);
            zeroFill (size1 + size2);
            return;
        }

        // Resampling path, chunked so the scratch stays small. Both channels'
        // interpolators consume identical input counts (same ratio, same output
        // count, reset together), so one bookkeeping covers both.
        int produced = 0;
        while (produced < numSamples)
        {
            const int chunkOut = juce::jmin (resampleChunk, numSamples - produced);
            const int needed   = (int) std::ceil (chunkOut * currentRatio) + 8;

            if (needed > scratchCapacity)   // absurd ratio — bail to silence
                break;

            // Top up the contiguous scratch from the FIFO.
            if (scratchFill < needed)
            {
                const int wanted = juce::jmin (needed - scratchFill,
                                               scratchCapacity - scratchFill);
                const int toRead = juce::jmin (fifo.getNumReady(), wanted);

                int start1, size1, start2, size2;
                fifo.prepareToRead (toRead, start1, size1, start2, size2);

                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto* source = ring.getReadPointer (ch);
                    auto* dst          = scratch.getWritePointer (ch) + scratchFill;
                    if (size1 > 0) juce::FloatVectorOperations::copy (dst,         source + start1, size1);
                    if (size2 > 0) juce::FloatVectorOperations::copy (dst + size1, source + start2, size2);
                }

                fifo.finishedRead (size1 + size2);
                scratchFill += size1 + size2;
            }

            if (scratchFill < needed)
                break;   // underrun — zero-fill the rest below

            int used = 0;
            for (int ch = 0; ch < numChannels && ch < 2; ++ch)
            {
                if (dest[ch] == nullptr)
                    continue;

                used = interpolators[(size_t) ch].process (currentRatio,
                                                           scratch.getReadPointer (ch),
                                                           dest[ch] + produced, chunkOut);
            }
            if (used <= 0 || used > scratchFill)
                break;

            // Shift the unconsumed samples to the scratch front (small move).
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = scratch.getWritePointer (ch);
                std::memmove (data, data + used, (size_t) (scratchFill - used) * sizeof (float));
            }
            scratchFill -= used;
            produced    += chunkOut;
        }

        zeroFill (produced);
    }

    //==========================================================================
    // Message thread. Is the tapped process currently producing output? This is
    // what separates the tap-decay bug (source IS outputting, yet the tap reads
    // exact zeros) from an ordinary pause (source stopped outputting — silence is
    // expected). Only the former warrants a disruptive rebuild. On < 14.4 the
    // process-object API is unavailable, but taps don't run there either.
    bool sourceIsRunningOutput()
    {
        if (__builtin_available (macOS 14.4, *))
        {
            const auto processObject = processObjectForPid ((pid_t) pid.load());
            if (processObject == kAudioObjectUnknown)
                return false;

            UInt32 runningOutput = 0;
            getProperty (processObject, kAudioProcessPropertyIsRunningOutput, runningOutput);
            return runningOutput != 0;
        }

        return true;
    }

    //==========================================================================
    // Watchdog (message thread), covering the two ways a tap dies in the wild:
    //
    //  • STALL — the aggregate stops calling the IOProc altogether. Its clock-
    //    master subdevice is the default output captured at tap-start time, so
    //    unplugging that device (headphones, an interface) kills the callbacks.
    //    Zero frames then never accrue, so only a liveness stamp can catch it.
    //    Rebuilding re-resolves the CURRENT default output, following the move.
    //
    //  • SILENCE — long-running taps have been reported to decay into permanent
    //    exact zeros while the IOProc keeps firing. After `rebuildAfterSeconds`
    //    of EXACT silence, tear down and recreate the tap — but only if the source
    //    is still outputting (see sourceIsRunningOutput; a paused source reads as
    //    silence too and must not trigger a teardown). Backoff doubles the
    //    threshold so we never storm; real samples reset the streak (in the
    //    IOProc) and both backoffs.
    void timerCallback() override
    {
        if (! active.load())
            return;

        // Unsigned subtraction is wraparound-safe.
        const auto sinceCallbackSeconds =
            (double) (juce::Time::getMillisecondCounter()
                        - lastCallbackMs.load (std::memory_order_relaxed)) / 1000.0;

        if (sinceCallbackSeconds >= stallAfterSeconds)
        {
            rebuild ("IOProc stalled");
            stallAfterSeconds = juce::jmin (stallAfterSeconds * 2.0, 60.0);
            return;
        }

        const auto zeroSeconds = (double) zeroFrames.load (std::memory_order_relaxed)
                                    / juce::jmax (1.0, tapRate);

        if (zeroSeconds <= 0.0)
        {
            rebuildAfterSeconds = 15.0;   // signal is flowing again
            stallAfterSeconds   = 5.0;
            return;
        }

        if (zeroSeconds < rebuildAfterSeconds)
            return;

        // A source that has stopped outputting (paused, between tracks, a quiet
        // passage) reads as exact silence too. Rebuilding then would tear the tap
        // down and briefly un-mute the app for nothing — and risks leaking dry
        // audio if playback resumes mid-rebuild. Only rebuild when the source is
        // actively outputting yet we still see silence: the genuine decay bug.
        if (! sourceIsRunningOutput())
            return;

        rebuild ("sustained exact silence");
        rebuildAfterSeconds = juce::jmin (rebuildAfterSeconds * 2.0, 240.0);
    }

    void rebuild (const char* why)
    {
        DBG ("ProcessTap: rebuilding tap (" << why << ")");
        juce::ignoreUnused (why);

        destroyCoreAudioObjects();
        const bool ok = createTap() && createAggregateAndStart();

        zeroFrames.store (0, std::memory_order_relaxed);
        lastCallbackMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
        resamplerNeedsReset.store (true, std::memory_order_release);

        if (! ok)
        {
            destroyCoreAudioObjects();
            DBG ("ProcessTap: rebuild failed; will retry after backoff");
        }
    }

    //==========================================================================
    std::atomic<juce::uint32> pid { 0 };
    double engineRate = 48000.0;
    double tapRate    = 48000.0;

    AudioObjectID tap       = kAudioObjectUnknown;
    AudioObjectID aggregate = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProcID = nullptr;
    NSString* tapUID = nil;

    // Sized once at the maximum so the ring/FIFO are never reallocated on a
    // re-start or rebuild: 524288 >= nextPowerOfTwo(384000), the highest tap rate
    // we accept. ~4 MB of stereo float — a one-time cost.
    static constexpr int maxRingFrames = 1 << 19;

    juce::AbstractFifo fifo { 1 };
    juce::AudioBuffer<float> ring;   // deinterleaved stereo at the tap rate

    std::atomic<bool>   active { false };
    std::atomic<double> ratio { 1.0 };
    std::atomic<juce::int64> zeroFrames { 0 };
    std::atomic<juce::uint32> lastCallbackMs { 0 };   // watchdog liveness stamp
    std::atomic<bool> resamplerNeedsReset { false };
    double rebuildAfterSeconds = 15.0;
    double stallAfterSeconds   = 5.0;

    // Resampler state (audio thread only).
    static constexpr int resampleChunk   = 512;
    static constexpr int scratchCapacity = resampleChunk * 8 + 64;   // supports ratios up to ~8x
    std::array<juce::LagrangeInterpolator, 2> interpolators;
    juce::AudioBuffer<float> scratch;
    int scratchFill = 0;
};

//==============================================================================
ProcessTapCapture::ProcessTapCapture()  : impl (std::make_unique<Impl>()) {}
ProcessTapCapture::~ProcessTapCapture() { impl->shutdown(); }

bool ProcessTapCapture::isSupported()
{
    if (__builtin_available (macOS 14.4, *))
        return true;

    return false;
}

bool ProcessTapCapture::start (juce::uint32 targetPid, double sampleRate)
{
    if (! isSupported())
        return false;

    return impl->start (targetPid, juce::jlimit (8000.0, 384000.0, sampleRate));
}

void ProcessTapCapture::stop()
{
    impl->shutdown();
}

void ProcessTapCapture::setEngineRate (double sampleRate)
{
    impl->setEngineRate (juce::jlimit (8000.0, 384000.0, sampleRate));
}

bool ProcessTapCapture::isActive() const noexcept
{
    return impl->active.load (std::memory_order_acquire);
}

juce::uint32 ProcessTapCapture::targetPid() const noexcept
{
    return impl->pid.load();
}

void ProcessTapCapture::read (float* const* dest, int numChannels, int numSamples) noexcept
{
    impl->read (dest, numChannels, numSamples);
}

bool ProcessTapCapture::isProcessRunning (juce::uint32 pid)
{
    if (pid == 0)
        return false;

    // Signal 0 performs the permission/existence check without sending anything;
    // EPERM still means "exists" (just not ours to signal).
    return kill ((pid_t) pid, 0) == 0 || errno == EPERM;
}

} // namespace play

#endif // JUCE_MAC

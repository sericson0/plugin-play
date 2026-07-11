#include "LoopbackCapture.h"

#if JUCE_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <psapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>

// ActivateAudioInterfaceAsync lives in mmdevapi.lib; the rest of WASAPI/COM is
// already linked by JUCE's audio_devices module.
#pragma comment (lib, "mmdevapi.lib")

namespace play
{
using Microsoft::WRL::ComPtr;

// JUCE runs the message thread as an STA, so CoInitializeEx usually returns
// RPC_E_CHANGED_MODE here — COM is already live; we just avoid an unbalanced
// CoUninitialize. (Same pattern as WasapiEndpoints.cpp.)
struct ScopedCom
{
    ScopedCom()  { ownsInit = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED)); }
    ~ScopedCom() { if (ownsInit) CoUninitialize(); }
    bool ownsInit = false;
};

//==============================================================================
static juce::String exeNameForPid (DWORD processId)
{
    if (processId == 0)
        return "(system)";

    HANDLE h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (h == nullptr)
        return {};

    wchar_t path[MAX_PATH]; DWORD sz = MAX_PATH;
    juce::String name;
    if (QueryFullProcessImageNameW (h, 0, path, &sz))
        name = juce::File (juce::String (path)).getFileName();

    CloseHandle (h);
    return name;
}

std::vector<AudioSource> enumerateAudioSources()
{
    std::vector<AudioSource> sources;
    ScopedCom com;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS (enumerator.GetAddressOf()))))
        return sources;

    ComPtr<IMMDevice> device;
    if (FAILED (enumerator->GetDefaultAudioEndpoint (eRender, eConsole, device.GetAddressOf())))
        return sources;

    ComPtr<IAudioSessionManager2> manager;
    if (FAILED (device->Activate (__uuidof (IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                  (void**) manager.GetAddressOf())))
        return sources;

    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED (manager->GetSessionEnumerator (sessions.GetAddressOf())))
        return sources;

    int count = 0;
    sessions->GetCount (&count);

    for (int i = 0; i < count; ++i)
    {
        ComPtr<IAudioSessionControl> control;
        if (FAILED (sessions->GetSession (i, control.GetAddressOf())))
            continue;

        ComPtr<IAudioSessionControl2> control2;
        if (FAILED (control.As (&control2)))
            continue;

        if (control2->IsSystemSoundsSession() == S_OK)   // skip Windows dings
            continue;

        DWORD sessionPid = 0;
        control2->GetProcessId (&sessionPid);
        if (sessionPid == 0)
            continue;

        // Never list Plugin Play itself — routing our own output back into our
        // input would only build a feedback loop.
        if (sessionPid == GetCurrentProcessId())
            continue;

        AudioSessionState state = AudioSessionStateInactive;
        control->GetState (&state);

        LPWSTR display = nullptr;
        juce::String displayName;
        if (SUCCEEDED (control->GetDisplayName (&display)) && display != nullptr && display[0] != 0)
            displayName = juce::String (display);
        if (display != nullptr)
            CoTaskMemFree (display);

        AudioSource source;
        source.pid         = (juce::uint32) sessionPid;
        source.executable  = exeNameForPid (sessionPid);
        source.displayName = displayName;
        source.active      = (state == AudioSessionStateActive);

        // A process can own more than one session; keep just the first (and prefer
        // an active one) so the picker shows one entry per app.
        auto existing = std::find_if (sources.begin(), sources.end(),
                                      [&] (const AudioSource& s) { return s.pid == source.pid; });
        if (existing != sources.end())
        {
            existing->active = existing->active || source.active;
            continue;
        }

        sources.push_back (source);
    }

    // Active sources first, then alphabetical — the app the DJ is using floats up.
    std::stable_sort (sources.begin(), sources.end(),
                      [] (const AudioSource& a, const AudioSource& b)
                      {
                          if (a.active != b.active) return a.active;
                          return a.executable.compareIgnoreCase (b.executable) < 0;
                      });

    return sources;
}

//==============================================================================
// Receives the async IAudioClient from ActivateAudioInterfaceAsync.
class ActivateHandler
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler>
{
public:
    HANDLE done = CreateEventW (nullptr, TRUE, FALSE, nullptr);
    HRESULT activateHr = E_FAIL;
    ComPtr<IAudioClient> client;

    STDMETHOD (ActivateCompleted) (IActivateAudioInterfaceAsyncOperation* op) override
    {
        IUnknown* unk = nullptr;
        op->GetActivateResult (&activateHr, &unk);
        if (SUCCEEDED (activateHr) && unk != nullptr)
        {
            unk->QueryInterface (IID_PPV_ARGS (client.GetAddressOf()));
            unk->Release();
        }
        SetEvent (done);
        return S_OK;
    }

    ~ActivateHandler() override { if (done != nullptr) CloseHandle (done); }
};

// Mute/unmute the default render endpoint's master. Returns the previous mute
// state (0/1), or -1 on failure. IAudioEndpointVolume master mute does NOT affect
// process-loopback capture (proven by ear) — that's what makes it the dry-kill.
static int setDefaultRenderMute (BOOL mute)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS (enumerator.GetAddressOf()))))
        return -1;

    ComPtr<IMMDevice> device;
    if (FAILED (enumerator->GetDefaultAudioEndpoint (eRender, eConsole, device.GetAddressOf())))
        return -1;

    ComPtr<IAudioEndpointVolume> volume;
    if (FAILED (device->Activate (__uuidof (IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                  (void**) volume.GetAddressOf())))
        return -1;

    BOOL prev = FALSE;
    volume->GetMute (&prev);
    if (FAILED (volume->SetMute (mute, nullptr)))
        return -1;

    return prev ? 1 : 0;
}

//==============================================================================
LoopbackCapture::LoopbackCapture() = default;

LoopbackCapture::~LoopbackCapture() { stop(); }

bool LoopbackCapture::start (juce::uint32 targetPid, double sampleRate)
{
    stop();

    captureRate = juce::jlimit (8000.0, 384000.0, sampleRate);
    pid.store (targetPid);

    // ~1 second of stereo headroom: absorbs clock drift and scheduling jitter
    // between the WASAPI capture thread and the output device callback.
    const int ringFrames = juce::nextPowerOfTwo ((int) captureRate);
    ring.setSize (2, ringFrames, false, true, true);
    ring.clear();
    fifo.setTotalSize (ringFrames);
    fifo.reset();

    stopFlag.store (false);
    active.store (true);

    // Mute the dry signal at the speakers before audio starts flowing.
    {
        ScopedCom com;
        previousEndpointMute = setDefaultRenderMute (TRUE);
    }

    captureThread = std::thread ([this] { captureThreadEntry(); });
    return true;
}

void LoopbackCapture::stop()
{
    stopFlag.store (true);

    if (captureThread.joinable())
        captureThread.join();

    if (active.load())
    {
        ScopedCom com;
        if (previousEndpointMute == 0)   // it was unmuted before we muted it
            setDefaultRenderMute (FALSE);
        previousEndpointMute = -1;
    }

    active.store (false);
    pid.store (0);
}

void LoopbackCapture::read (float* const* dest, int numChannels, int numSamples) noexcept
{
    const int ringChans = ring.getNumChannels();

    // Not capturing — either the quarantined path is disabled (start() never ran, so
    // ring is empty) or we're between stop() and start(). Emit silence and bail before
    // touching the FIFO/ring: otherwise jmin(ch, ringChans-1) indexes channel -1 on an
    // unsized buffer (undefined behaviour, and a debug assert on every audio block).
    if (! active.load (std::memory_order_acquire) || ringChans <= 0)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            if (dest[ch] != nullptr)
                juce::FloatVectorOperations::clear (dest[ch], numSamples);
        return;
    }

    const int ready     = fifo.getNumReady();
    const int toRead    = juce::jmin (ready, numSamples);

    int start1, size1, start2, size2;
    fifo.prepareToRead (toRead, start1, size1, start2, size2);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (dest[ch] == nullptr)
            continue;

        const float* src = ring.getReadPointer (juce::jmin (ch, ringChans - 1));

        if (size1 > 0) juce::FloatVectorOperations::copy (dest[ch],          src + start1, size1);
        if (size2 > 0) juce::FloatVectorOperations::copy (dest[ch] + size1,  src + start2, size2);

        // Underrun: fill the shortfall with silence rather than stale audio.
        if (toRead < numSamples)
            juce::FloatVectorOperations::clear (dest[ch] + toRead, numSamples - toRead);
    }

    fifo.finishedRead (toRead);
}

void LoopbackCapture::captureThreadEntry()
{
    if (FAILED (CoInitializeEx (nullptr, COINIT_MULTITHREADED)))
        return;

    // --- Activate process-loopback capture on the target PID ------------------
    AUDIOCLIENT_ACTIVATION_PARAMS params {};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId    = (DWORD) pid.load();
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv {};
    pv.vt = VT_BLOB;
    pv.blob.cbSize    = sizeof (params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*> (&params);

    auto handler = Microsoft::WRL::Make<ActivateHandler>();
    ComPtr<IActivateAudioInterfaceAsyncOperation> op;
    if (FAILED (ActivateAudioInterfaceAsync (VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                             __uuidof (IAudioClient), &pv, handler.Get(),
                                             op.GetAddressOf())))
    {
        CoUninitialize();
        return;
    }

    WaitForSingleObject (handler->done, INFINITE);
    if (FAILED (handler->activateHr) || handler->client == nullptr)
    {
        CoUninitialize();
        return;
    }

    // Process loopback has no device mix format — we dictate it, matching the
    // output device rate so Windows resamples the source, not us.
    WAVEFORMATEX fmt {};
    fmt.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
    fmt.nChannels       = 2;
    fmt.nSamplesPerSec  = (DWORD) captureRate;
    fmt.wBitsPerSample  = 32;
    fmt.nBlockAlign     = (WORD) (fmt.nChannels * fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    auto* client = handler->client.Get();

    if (FAILED (client->Initialize (AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                    2000000, 0, &fmt, nullptr)))
    {
        CoUninitialize();
        return;
    }

    HANDLE bufferEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr);
    client->SetEventHandle (bufferEvent);

    ComPtr<IAudioCaptureClient> capture;
    if (FAILED (client->GetService (IID_PPV_ARGS (capture.GetAddressOf()))))
    {
        CloseHandle (bufferEvent);
        CoUninitialize();
        return;
    }

    if (FAILED (client->Start()))
    {
        CloseHandle (bufferEvent);
        CoUninitialize();
        return;
    }

    // --- Pump packets into the FIFO until asked to stop -----------------------
    while (! stopFlag.load())
    {
        WaitForSingleObject (bufferEvent, 100);

        UINT32 packet = 0;
        while (SUCCEEDED (capture->GetNextPacketSize (&packet)) && packet > 0)
        {
            BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
            if (FAILED (capture->GetBuffer (&data, &frames, &flags, nullptr, nullptr)))
                break;

            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

            int start1, size1, start2, size2;
            fifo.prepareToWrite ((int) frames, start1, size1, start2, size2);
            const int writable = size1 + size2;   // may be < frames on overrun (drop excess)

            auto deinterleave = [&] (int ringStart, int frameOffset, int numFrames)
            {
                float* l = ring.getWritePointer (0);
                float* r = ring.getWritePointer (1);
                const float* interleaved = reinterpret_cast<const float*> (data);

                for (int n = 0; n < numFrames; ++n)
                {
                    if (silent || data == nullptr)
                    {
                        l[ringStart + n] = 0.0f;
                        r[ringStart + n] = 0.0f;
                    }
                    else
                    {
                        const float* frame = interleaved + (size_t) (frameOffset + n) * 2;
                        l[ringStart + n] = frame[0];
                        r[ringStart + n] = frame[1];
                    }
                }
            };

            if (size1 > 0) deinterleave (start1, 0,     size1);
            if (size2 > 0) deinterleave (start2, size1, size2);

            fifo.finishedWrite (writable);
            capture->ReleaseBuffer (frames);
        }
    }

    client->Stop();
    CloseHandle (bufferEvent);
    CoUninitialize();
}

} // namespace play

#else //=======================================================================

namespace play
{
    std::vector<AudioSource> enumerateAudioSources() { return {}; }

    LoopbackCapture::LoopbackCapture() = default;
    LoopbackCapture::~LoopbackCapture() = default;
    bool LoopbackCapture::start (juce::uint32, double) { return false; }
    void LoopbackCapture::stop() {}
    void LoopbackCapture::read (float* const* dest, int numChannels, int numSamples) noexcept
    {
        for (int ch = 0; ch < numChannels; ++ch)
            if (dest[ch] != nullptr)
                juce::FloatVectorOperations::clear (dest[ch], numSamples);
    }
}

#endif

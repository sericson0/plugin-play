// M2 experiment: driverless capture for Plugin Play.
//
// Question under test: can we capture a specific process's audio via
// AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK (Win10 2004+, zero drivers)
// AND silence its dry signal at the speakers by muting its audio session,
// while the process-loopback capture keeps receiving audio?
//
// Method: two simultaneous captures on a target PID that is playing a tone:
//   A) process loopback of the target PID  (what Plugin Play would ingest)
//   B) endpoint loopback of the default render device (what hits the speakers)
// Phases: 1) unmuted  2) session muted  3) unmuted again (recovery check).
//
// Verdict PASS = A stays hot in phase 2 while B drops to silence.
//
// Usage: loopback_test.exe <pid> [phaseSeconds]

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::FtmBase;

static void checkHR(HRESULT hr, const char* what)
{
    if (FAILED(hr))
    {
        fprintf(stderr, "FAILED (hr=0x%08lX): %s\n", (unsigned long) hr, what);
        exit(2);
    }
}

// ---------------------------------------------------------------- phase state
// -1 = transition (ignore samples), 0..2 = measurement phase
static std::atomic<int>  g_phase { -1 };
static std::atomic<bool> g_stop  { false };

struct StreamStats
{
    std::atomic<float>    peak[3]   {};   // max |sample| seen per phase
    std::atomic<uint64_t> frames[3] {};   // frames delivered per phase

    void note(float p, uint64_t nFrames)
    {
        int ph = g_phase.load(std::memory_order_relaxed);
        if (ph < 0 || ph > 2)
            return;
        float cur = peak[ph].load(std::memory_order_relaxed);
        while (p > cur && ! peak[ph].compare_exchange_weak(cur, p)) {}
        frames[ph] += nFrames;
    }
};

static StreamStats g_procStats;  // A: process loopback
static StreamStats g_endStats;   // B: endpoint loopback

static float peakOfFloatBuffer(const BYTE* data, UINT32 numFrames, int channels, DWORD flags)
{
    if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
        return 0.0f;
    const float* s = reinterpret_cast<const float*>(data);
    float peak = 0.0f;
    for (UINT32 i = 0; i < numFrames * (UINT32) channels; ++i)
    {
        float a = fabsf(s[i]);
        if (a > peak) peak = a;
    }
    return peak;
}

// ------------------------------------------------- A: process loopback capture
class ActivateHandler
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
                          IActivateAudioInterfaceCompletionHandler>
{
public:
    HANDLE              done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT             activateHr = E_FAIL;
    ComPtr<IAudioClient> client;

    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* op) override
    {
        IUnknown* unk = nullptr;
        op->GetActivateResult(&activateHr, &unk);
        if (SUCCEEDED(activateHr) && unk != nullptr)
        {
            unk->QueryInterface(IID_PPV_ARGS(&client));
            unk->Release();
        }
        SetEvent(done);
        return S_OK;
    }
};

static void processLoopbackThread(DWORD pid)
{
    checkHR(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx (proc thread)");

    AUDIOCLIENT_ACTIVATION_PARAMS params {};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv {};
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    auto handler = Microsoft::WRL::Make<ActivateHandler>();
    ComPtr<IActivateAudioInterfaceAsyncOperation> op;
    checkHR(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                        __uuidof(IAudioClient), &pv, handler.Get(), &op),
            "ActivateAudioInterfaceAsync(PROCESS_LOOPBACK)");
    WaitForSingleObject(handler->done, INFINITE);
    checkHR(handler->activateHr, "process-loopback activation result");

    // Process loopback has no device mix format; we dictate the format.
    WAVEFORMATEX fmt {};
    fmt.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
    fmt.nChannels       = 2;
    fmt.nSamplesPerSec  = 48000;
    fmt.wBitsPerSample  = 32;
    fmt.nBlockAlign     = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    checkHR(handler->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                        2000000, 0, &fmt, nullptr),
            "IAudioClient::Initialize (process loopback)");

    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    checkHR(handler->client->SetEventHandle(ev), "SetEventHandle");

    ComPtr<IAudioCaptureClient> cap;
    checkHR(handler->client->GetService(IID_PPV_ARGS(&cap)), "GetService(IAudioCaptureClient) proc");
    checkHR(handler->client->Start(), "Start (process loopback)");

    while (! g_stop)
    {
        WaitForSingleObject(ev, 100);
        UINT32 pkt = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&pkt)) && pkt > 0)
        {
            BYTE* data; UINT32 nFrames; DWORD flags;
            if (FAILED(cap->GetBuffer(&data, &nFrames, &flags, nullptr, nullptr)))
                break;
            g_procStats.note(peakOfFloatBuffer(data, nFrames, fmt.nChannels, flags), nFrames);
            cap->ReleaseBuffer(nFrames);
        }
    }
    handler->client->Stop();
    CoUninitialize();
}

// ------------------------------------------------ B: endpoint loopback capture
static void endpointLoopbackThread()
{
    checkHR(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx (endpoint thread)");

    ComPtr<IMMDeviceEnumerator> enumr;
    checkHR(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                             IID_PPV_ARGS(&enumr)), "create MMDeviceEnumerator");
    ComPtr<IMMDevice> dev;
    checkHR(enumr->GetDefaultAudioEndpoint(eRender, eConsole, &dev), "GetDefaultAudioEndpoint");

    ComPtr<IAudioClient> client;
    checkHR(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**) client.GetAddressOf()),
            "Activate IAudioClient (endpoint)");

    WAVEFORMATEX* mix = nullptr;
    checkHR(client->GetMixFormat(&mix), "GetMixFormat");

    // Shared-mode mix format is float32 in practice; verify so peak math is valid.
    bool isFloat = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        static const GUID ieeeFloat = // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
            { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
        isFloat = memcmp(&reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat,
                         &ieeeFloat, sizeof(GUID)) == 0;
    }
    if (! isFloat)
    {
        fprintf(stderr, "Endpoint mix format is not float32; extend the test.\n");
        exit(2);
    }

    checkHR(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                               2000000, 0, mix, nullptr),
            "IAudioClient::Initialize (endpoint loopback)");

    ComPtr<IAudioCaptureClient> cap;
    checkHR(client->GetService(IID_PPV_ARGS(&cap)), "GetService(IAudioCaptureClient) endpoint");
    checkHR(client->Start(), "Start (endpoint loopback)");

    while (! g_stop)
    {
        Sleep(10);  // polling mode: event-driven endpoint loopback stalls on silence
        UINT32 pkt = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&pkt)) && pkt > 0)
        {
            BYTE* data; UINT32 nFrames; DWORD flags;
            if (FAILED(cap->GetBuffer(&data, &nFrames, &flags, nullptr, nullptr)))
                break;
            g_endStats.note(peakOfFloatBuffer(data, nFrames, mix->nChannels, flags), nFrames);
            cap->ReleaseBuffer(nFrames);
        }
    }
    client->Stop();
    CoTaskMemFree(mix);
    CoUninitialize();
}

// ----------------------------------------------------------- session mute ctl
static bool setSessionMute(DWORD pid, BOOL mute)
{
    ComPtr<IMMDeviceEnumerator> enumr;
    checkHR(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                             IID_PPV_ARGS(&enumr)), "create MMDeviceEnumerator (mute)");
    ComPtr<IMMDevice> dev;
    checkHR(enumr->GetDefaultAudioEndpoint(eRender, eConsole, &dev), "GetDefaultAudioEndpoint (mute)");

    ComPtr<IAudioSessionManager2> mgr;
    checkHR(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                          (void**) mgr.GetAddressOf()), "Activate IAudioSessionManager2");

    ComPtr<IAudioSessionEnumerator> sessions;
    checkHR(mgr->GetSessionEnumerator(&sessions), "GetSessionEnumerator");

    int count = 0;
    sessions->GetCount(&count);
    bool found = false;
    for (int i = 0; i < count; ++i)
    {
        ComPtr<IAudioSessionControl> ctl;
        if (FAILED(sessions->GetSession(i, &ctl))) continue;
        ComPtr<IAudioSessionControl2> ctl2;
        if (FAILED(ctl.As(&ctl2))) continue;
        DWORD sessPid = 0;
        if (FAILED(ctl2->GetProcessId(&sessPid)) || sessPid != pid) continue;

        ComPtr<ISimpleAudioVolume> vol;
        if (SUCCEEDED(ctl.As(&vol)))
        {
            vol->SetMute(mute, nullptr);
            found = true;
        }
    }
    return found;
}

// ------------------------------------------------------- endpoint master mute
// Returns previous mute state so it can be restored.
static BOOL setEndpointMasterMute(BOOL mute)
{
    ComPtr<IMMDeviceEnumerator> enumr;
    checkHR(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                             IID_PPV_ARGS(&enumr)), "create MMDeviceEnumerator (epmute)");
    ComPtr<IMMDevice> dev;
    checkHR(enumr->GetDefaultAudioEndpoint(eRender, eConsole, &dev), "GetDefaultAudioEndpoint (epmute)");
    ComPtr<IAudioEndpointVolume> vol;
    checkHR(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                          (void**) vol.GetAddressOf()), "Activate IAudioEndpointVolume");
    BOOL prev = FALSE;
    vol->GetMute(&prev);
    checkHR(vol->SetMute(mute, nullptr), "IAudioEndpointVolume::SetMute");
    return prev;
}

// ---------------------------------------------------------------------- main
int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: loopback_test.exe <pid> [phaseSeconds]\n");
        return 1;
    }
    DWORD pid = (DWORD) strtoul(argv[1], nullptr, 10);
    int phaseSec = argc > 2 ? atoi(argv[2]) : 3;
    // mute lever under test: "session" (ISimpleAudioVolume, proven to also
    // silence the capture) or "endpoint" (IAudioEndpointVolume master mute)
    bool sessionMode = argc > 3 && strcmp(argv[3], "session") == 0;

    checkHR(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx (main)");

    printf("Target PID: %lu   phases: %ds each   mute lever: %s\n",
           (unsigned long) pid, phaseSec, sessionMode ? "SESSION" : "ENDPOINT MASTER");
    printf("NOTE: keep other system audio quiet during the test.\n\n");

    std::thread tA(processLoopbackThread, pid);
    std::thread tB(endpointLoopbackThread);

    auto runPhase = [&](int ph, const char* label)
    {
        Sleep(400);                 // let transition settle; samples ignored
        g_phase = ph;
        printf("phase %d (%s) ... ", ph, label);
        fflush(stdout);
        Sleep((DWORD) phaseSec * 1000);
        g_phase = -1;
        printf("proc peak %.4f (%llu frames)   endpoint peak %.4f (%llu frames)\n",
               g_procStats.peak[ph].load(), (unsigned long long) g_procStats.frames[ph].load(),
               g_endStats.peak[ph].load(),  (unsigned long long) g_endStats.frames[ph].load());
    };

    runPhase(0, "unmuted");

    BOOL prevMasterMute = FALSE;
    if (sessionMode)
    {
        if (! setSessionMute(pid, TRUE))
        {
            fprintf(stderr, "Could not find audio session for PID %lu to mute!\n", (unsigned long) pid);
            g_stop = true; tA.join(); tB.join();
            return 2;
        }
        runPhase(1, "session MUTED");
        setSessionMute(pid, FALSE);
    }
    else
    {
        prevMasterMute = setEndpointMasterMute(TRUE);
        printf("  (endpoint master muted — LISTEN: tone should be inaudible now)\n");
        runPhase(1, "endpoint master MUTED");
        setEndpointMasterMute(prevMasterMute);
    }
    runPhase(2, "unmuted again");

    g_stop = true;
    tA.join();
    tB.join();

    // ------------------------------------------------------------- verdict
    float p0a = g_procStats.peak[0], p1a = g_procStats.peak[1], p2a = g_procStats.peak[2];
    float p0b = g_endStats.peak[0],  p1b = g_endStats.peak[1];

    printf("\n==== VERDICT ====\n");
    bool captureWorks = p0a > 0.02f;
    bool survivesMute = p1a > 0.5f * p0a;
    bool recovers     = p2a > 0.5f * p0a;

    printf("process-loopback capture works:        %s (peak %.4f)\n", captureWorks ? "YES" : "NO", p0a);
    printf("capture SURVIVES mute:                 %s (%.4f vs %.4f)\n", survivesMute ? "YES" : "NO", p1a, p0a);
    printf("capture recovers after unmute:         %s (peak %.4f)\n", recovers ? "YES" : "NO", p2a);
    printf("endpoint loopback (informational):     unmuted %.4f / muted %.4f\n", p0b, p1b);
    if (! sessionMode)
        printf("NOTE: endpoint loopback taps pre-master-volume; silence at the\n"
               "      speakers during phase 1 must be confirmed BY EAR.\n");

    bool viable = captureWorks && survivesMute && recovers;
    printf("\n>>> %s-MUTE LEVER: %s <<<\n",
           sessionMode ? "SESSION" : "ENDPOINT",
           viable ? "capture keeps flowing while muted — VIABLE (confirm silence by ear)"
                  : "NOT VIABLE — mute also silences the capture");
    return viable ? 0 : 3;
}

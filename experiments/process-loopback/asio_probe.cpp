// M2 experiment (part 2): can Plugin Play drive an external DAC over ASIO
// for its OUTPUT stage — and does that coexist with process-loopback capture
// (which would be running on the DJ app on a DIFFERENT endpoint)?
//
// What it does:
//   1. Finds an ASIO driver by name substring (default "Universal Audio Thunderbolt")
//      from HKLM\SOFTWARE\ASIO, CoCreateInstance's it (IID == CLSID for ASIO).
//   2. init() -> reports name/version, channels, sample rate, buffer sizes, latency,
//      and the output sample format.
//   3. createBuffers on 2 output channels + streams an audible 440 Hz tone so the
//      DAC connection can be confirmed BY EAR.
//   4. Optional: --capture <pid> runs process-loopback capture concurrently and
//      reports whether capture kept flowing while ASIO output was live.
//
// Usage: asio_probe.exe ["driver name substr"] [seconds] [--capture <pid>]

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include "iasiodrv.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <utility>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::FtmBase;

// ------------------------------------------------------------- ASIO globals
// ASIO callbacks are C function pointers, so driver state lives at file scope.
static IASIO*                  g_asio        = nullptr;
static long                    g_bufSize     = 0;
static long                    g_numOut      = 0;
static double                  g_sampleRate  = 48000.0;
static ASIOBufferInfo          g_bufInfos[2] {};
static ASIOSampleType          g_outType     = ASIOSTInt32LSB;
static ASIOCallbacks           g_cb          {};
static double                  g_phase       = 0.0;
static std::atomic<bool>       g_asioRunning { false };
static std::atomic<uint64_t>   g_asioBufferSwitches { 0 };
static bool                    g_postOutputReady = false;

static const double kToneHz = 440.0;
static const double kAmp     = 0.20;

static void writeSample(void* buf, long frame, double v)
{
    switch (g_outType)
    {
        case ASIOSTInt32LSB:
            ((int32_t*) buf)[frame] = (int32_t) (v * 2147483647.0);
            break;
        case ASIOSTInt16LSB:
            ((int16_t*) buf)[frame] = (int16_t) (v * 32767.0);
            break;
        case ASIOSTFloat32LSB:
            ((float*) buf)[frame] = (float) v;
            break;
        case ASIOSTInt24LSB:
        {
            int32_t s = (int32_t) (v * 8388607.0);
            uint8_t* p = ((uint8_t*) buf) + frame * 3;
            p[0] = (uint8_t) (s & 0xff);
            p[1] = (uint8_t) ((s >> 8) & 0xff);
            p[2] = (uint8_t) ((s >> 16) & 0xff);
            break;
        }
        // 32-bit container, data left-aligned in the high bits
        case ASIOSTInt32LSB16: ((int32_t*) buf)[frame] = (int32_t)(v * 32767.0)        << 16; break;
        case ASIOSTInt32LSB18: ((int32_t*) buf)[frame] = (int32_t)(v * 131071.0)       << 14; break;
        case ASIOSTInt32LSB20: ((int32_t*) buf)[frame] = (int32_t)(v * 524287.0)       << 12; break;
        case ASIOSTInt32LSB24: ((int32_t*) buf)[frame] = (int32_t)(v * 8388607.0)      << 8;  break;
        default:
            ((int32_t*) buf)[frame] = (int32_t) (v * 2147483647.0);
            break;
    }
}

static void bufferSwitchImpl(long index)
{
    double inc = 2.0 * 3.14159265358979323846 * kToneHz / g_sampleRate;
    for (long f = 0; f < g_bufSize; ++f)
    {
        double v = g_asioRunning ? kAmp * sin(g_phase) : 0.0;
        g_phase += inc;
        if (g_phase > 2.0 * 3.14159265358979323846) g_phase -= 2.0 * 3.14159265358979323846;
        for (long ch = 0; ch < g_numOut; ++ch)
            writeSample(g_bufInfos[ch].buffers[index], f, v);
    }
    if (g_postOutputReady)
        g_asio->outputReady();
    g_asioBufferSwitches++;
}

static void cbBufferSwitch(long index, ASIOBool) { bufferSwitchImpl(index); }
static void cbSampleRateDidChange(ASIOSampleRate) {}
static long cbAsioMessage(long selector, long value, void*, double*)
{
    switch (selector)
    {
        case kAsioSelectorSupported:
            return (value == kAsioResetRequest || value == kAsioEngineVersion
                    || value == kAsioResyncRequest || value == kAsioLatenciesChanged) ? 1 : 0;
        case kAsioEngineVersion:      return 2;
        case kAsioResetRequest:       return 1;
        case kAsioResyncRequest:      return 1;
        case kAsioLatenciesChanged:   return 1;
        case kAsioSupportsTimeInfo:   return 0;   // we use plain bufferSwitch
        default:                      return 0;
    }
}
static ASIOTime* cbBufferSwitchTimeInfo(ASIOTime*, long index, ASIOBool)
{
    bufferSwitchImpl(index);
    return nullptr;
}

// --------------------------------------------------------- registry: find CLSID
static bool findAsioClsid(const std::string& want, CLSID& out, std::string& matchedName)
{
    HKEY root;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &root) != ERROR_SUCCESS)
        return false;
    char sub[256];
    DWORD idx = 0, len = sizeof(sub);
    bool found = false;
    while (RegEnumKeyExA(root, idx++, sub, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
    {
        len = sizeof(sub);
        std::string name = sub;
        // case-insensitive substring match
        std::string lname = name, lwant = want;
        for (auto& c : lname) c = (char) tolower((unsigned char) c);
        for (auto& c : lwant) c = (char) tolower((unsigned char) c);
        if (lname.find(lwant) != std::string::npos)
        {
            HKEY k;
            if (RegOpenKeyExA(root, sub, 0, KEY_READ, &k) == ERROR_SUCCESS)
            {
                char clsidStr[64]; DWORD cb = sizeof(clsidStr), type = 0;
                if (RegQueryValueExA(k, "CLSID", nullptr, &type, (LPBYTE) clsidStr, &cb) == ERROR_SUCCESS)
                {
                    wchar_t wclsid[64];
                    MultiByteToWideChar(CP_ACP, 0, clsidStr, -1, wclsid, 64);
                    if (CLSIDFromString(wclsid, &out) == NOERROR)
                    {
                        matchedName = name;
                        found = true;
                    }
                }
                RegCloseKey(k);
            }
            if (found) break;
        }
    }
    RegCloseKey(root);
    return found;
}

// Enumerate ALL registered ASIO drivers (name + CLSID).
static std::vector<std::pair<std::string, CLSID>> listAsioDrivers()
{
    std::vector<std::pair<std::string, CLSID>> out;
    HKEY root;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &root) != ERROR_SUCCESS)
        return out;
    char sub[256]; DWORD idx = 0, len = sizeof(sub);
    while (RegEnumKeyExA(root, idx++, sub, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
    {
        len = sizeof(sub);
        HKEY k;
        if (RegOpenKeyExA(root, sub, 0, KEY_READ, &k) == ERROR_SUCCESS)
        {
            char clsidStr[64]; DWORD cb = sizeof(clsidStr), type = 0;
            if (RegQueryValueExA(k, "CLSID", nullptr, &type, (LPBYTE) clsidStr, &cb) == ERROR_SUCCESS)
            {
                wchar_t w[64]; MultiByteToWideChar(CP_ACP, 0, clsidStr, -1, w, 64);
                CLSID c;
                if (CLSIDFromString(w, &c) == NOERROR) out.push_back({ sub, c });
            }
            RegCloseKey(k);
        }
    }
    RegCloseKey(root);
    return out;
}

static const char* sampleTypeName(ASIOSampleType t)
{
    switch (t)
    {
        case ASIOSTInt16LSB:   return "Int16LSB";
        case ASIOSTInt24LSB:   return "Int24LSB";
        case ASIOSTInt32LSB:   return "Int32LSB";
        case ASIOSTFloat32LSB: return "Float32LSB";
        case ASIOSTFloat64LSB: return "Float64LSB";
        case ASIOSTInt32LSB16: return "Int32LSB16";
        case ASIOSTInt32LSB18: return "Int32LSB18";
        case ASIOSTInt32LSB20: return "Int32LSB20";
        case ASIOSTInt32LSB24: return "Int32LSB24";
        default:               return "other/unknown";
    }
}

// ----------------------------------------- optional concurrent process capture
static std::atomic<bool>  g_capStop { false };
static std::atomic<float> g_capPeak { 0.0f };
static std::atomic<uint64_t> g_capFrames { 0 };

class ActivateHandler
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
                          IActivateAudioInterfaceCompletionHandler>
{
public:
    HANDLE               done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT              hr   = E_FAIL;
    ComPtr<IAudioClient> client;
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* op) override
    {
        IUnknown* unk = nullptr;
        op->GetActivateResult(&hr, &unk);
        if (SUCCEEDED(hr) && unk) { unk->QueryInterface(IID_PPV_ARGS(&client)); unk->Release(); }
        SetEvent(done);
        return S_OK;
    }
};

static void captureThread(DWORD pid)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    AUDIOCLIENT_ACTIVATION_PARAMS ap {};
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId = pid;
    ap.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT pv {}; pv.vt = VT_BLOB; pv.blob.cbSize = sizeof(ap); pv.blob.pBlobData = (BYTE*) &ap;

    auto h = Microsoft::WRL::Make<ActivateHandler>();
    ComPtr<IActivateAudioInterfaceAsyncOperation> op;
    if (FAILED(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                           __uuidof(IAudioClient), &pv, h.Get(), &op)))
    { CoUninitialize(); return; }
    WaitForSingleObject(h->done, INFINITE);
    if (FAILED(h->hr)) { CoUninitialize(); return; }

    WAVEFORMATEX fmt {};
    fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; fmt.nChannels = 2; fmt.nSamplesPerSec = 48000;
    fmt.wBitsPerSample = 32; fmt.nBlockAlign = 8; fmt.nAvgBytesPerSec = 48000 * 8;
    if (FAILED(h->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
              AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
              2000000, 0, &fmt, nullptr))) { CoUninitialize(); return; }
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    h->client->SetEventHandle(ev);
    ComPtr<IAudioCaptureClient> cap;
    h->client->GetService(IID_PPV_ARGS(&cap));
    h->client->Start();
    while (! g_capStop)
    {
        WaitForSingleObject(ev, 100);
        UINT32 pkt = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&pkt)) && pkt > 0)
        {
            BYTE* data; UINT32 n; DWORD flags;
            if (FAILED(cap->GetBuffer(&data, &n, &flags, nullptr, nullptr))) break;
            if (! (flags & AUDCLNT_BUFFERFLAGS_SILENT))
            {
                const float* s = (const float*) data;
                float peak = g_capPeak.load();
                for (UINT32 i = 0; i < n * 2; ++i) { float a = fabsf(s[i]); if (a > peak) peak = a; }
                g_capPeak = peak;
                g_capFrames += n;
            }
        }
    }
    h->client->Stop();
    CoUninitialize();
}

// --------------------------------------------------------- hidden window (init)
static HWND makeHiddenWindow()
{
    WNDCLASSA wc {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "PluginPlayAsioProbe";
    RegisterClassA(&wc);
    return CreateWindowExA(0, wc.lpszClassName, "probe", WS_OVERLAPPED,
                           0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
}

// ---------------------------------------------------------------------- main
int main(int argc, char** argv)
{
    std::string want = "Universal Audio Thunderbolt";
    int seconds = 4;
    DWORD capturePid = 0;
    bool autoScan = false;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capturePid = (DWORD) strtoul(argv[++i], nullptr, 10);
        else if (strcmp(argv[i], "--auto") == 0)               autoScan = true;
        else if (argv[i][0] >= '0' && argv[i][0] <= '9')       seconds = atoi(argv[i]);
        else                                                    want = argv[i];
    }

    // ASIO drivers generally want an STA + a window handle for init().
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCo)) { fprintf(stderr, "CoInitializeEx failed\n"); return 2; }

    CLSID clsid; std::string matched;

    // --auto: try init() on every registered ASIO driver, pick the first that
    // actually connects to hardware. Makes the test one-shot once a DAC is plugged in.
    if (autoScan)
    {
        printf("Scanning all registered ASIO drivers for a live device...\n");
        bool live = false;
        for (auto& d : listAsioDrivers())
        {
            void* r = nullptr;
            if (FAILED(CoCreateInstance(d.second, nullptr, CLSCTX_INPROC_SERVER, d.second, &r)) || !r)
            { printf("  [skip]  %-32s (COM create failed)\n", d.first.c_str()); continue; }
            IASIO* a = (IASIO*) r;
            HWND w = makeHiddenWindow();
            if (a->init(w) == ASIOTrue)
            {
                printf("  [LIVE]  %-32s <== using this\n", d.first.c_str());
                clsid = d.second; matched = d.first; live = true;
                a->Release();
                break;
            }
            char err[256] = {0}; a->getErrorMessage(err);
            printf("  [dead]  %-32s (%s)\n", d.first.c_str(), err);
            a->Release();
        }
        if (! live)
        {
            fprintf(stderr, "\nNo ASIO driver has a connected device. Plug in / power on your DAC.\n");
            return 3;
        }
    }
    else if (! findAsioClsid(want, clsid, matched))
    {
        fprintf(stderr, "No ASIO driver matching \"%s\" found in registry.\n", want.c_str());
        return 2;
    }
    printf("Matched ASIO driver: \"%s\"\n", matched.c_str());

    // For ASIO, the interface IID equals the class CLSID.
    void* raw = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, &raw);
    if (FAILED(hr) || raw == nullptr)
    {
        fprintf(stderr, "CoCreateInstance failed (hr=0x%08lX). Driver DLL missing/unregistered?\n",
                (unsigned long) hr);
        return 2;
    }
    g_asio = (IASIO*) raw;

    HWND wnd = makeHiddenWindow();
    if (g_asio->init(wnd) != ASIOTrue)
    {
        char err[256] = {0}; g_asio->getErrorMessage(err);
        fprintf(stderr, "\n>>> ASIO init() FAILED: %s\n", err);
        fprintf(stderr, ">>> DAC likely not connected/powered, or grabbed by another app.\n");
        g_asio->Release();
        return 3;
    }
    char drvName[64] = {0}; g_asio->getDriverName(drvName);
    printf("Driver init OK: %s  (version %ld)\n", drvName, g_asio->getDriverVersion());

    long nIn = 0, nOut = 0;
    g_asio->getChannels(&nIn, &nOut);
    printf("Channels: %ld in / %ld out\n", nIn, nOut);
    if (nOut < 2) { fprintf(stderr, "Need >=2 output channels.\n"); g_asio->Release(); return 3; }

    g_asio->getSampleRate(&g_sampleRate);
    if (g_sampleRate < 8000.0 || g_sampleRate > 768000.0)
    {
        g_asio->setSampleRate(48000.0);
        g_asio->getSampleRate(&g_sampleRate);
    }
    long minB, maxB, prefB, gran;
    g_asio->getBufferSize(&minB, &maxB, &prefB, &gran);
    long inLat = 0, outLat = 0;
    g_asio->getLatencies(&inLat, &outLat);

    ASIOChannelInfo ci {}; ci.channel = 0; ci.isInput = ASIOFalse;
    g_asio->getChannelInfo(&ci);
    g_outType = ci.type;

    printf("Sample rate: %.0f Hz\n", g_sampleRate);
    printf("Buffer sizes: min %ld / max %ld / preferred %ld (granularity %ld)\n", minB, maxB, prefB, gran);
    printf("Reported latency: %ld in / %ld out samples (%.2f ms out @ %.0f Hz)\n",
           inLat, outLat, 1000.0 * outLat / g_sampleRate, g_sampleRate);
    printf("Output ch0 name: \"%s\"  sample type: %s\n", ci.name, sampleTypeName(g_outType));

    // does the driver want outputReady()?
    g_postOutputReady = (g_asio->outputReady() == ASE_OK);

    g_numOut  = 2;
    g_bufSize = prefB;
    for (long ch = 0; ch < 2; ++ch)
    {
        g_bufInfos[ch].isInput = ASIOFalse;
        g_bufInfos[ch].channelNum = ch;
    }
    g_cb.bufferSwitch         = cbBufferSwitch;
    g_cb.sampleRateDidChange  = cbSampleRateDidChange;
    g_cb.asioMessage          = cbAsioMessage;
    g_cb.bufferSwitchTimeInfo = cbBufferSwitchTimeInfo;

    ASIOError ce = g_asio->createBuffers(g_bufInfos, 2, g_bufSize, &g_cb);
    if (ce != ASE_OK) { fprintf(stderr, "createBuffers failed (%ld)\n", ce); g_asio->Release(); return 3; }

    // optional coexistence: start capturing a DJ-app PID on a different endpoint
    std::thread capThread;
    if (capturePid)
    {
        printf("\nCoexistence test: also capturing PID %lu via process loopback...\n", (unsigned long) capturePid);
        capThread = std::thread(captureThread, capturePid);
    }

    printf("\n>>> Streaming %d s of 440 Hz tone to the DAC — LISTEN for it. <<<\n", seconds);
    g_asioRunning = true;
    if (g_asio->start() != ASE_OK)
    {
        fprintf(stderr, "ASIO start() failed\n");
        g_asio->disposeBuffers(); g_asio->Release(); return 3;
    }

    // pump messages so the driver is happy; run for the requested duration
    DWORD end = GetTickCount() + (DWORD) seconds * 1000;
    MSG msg;
    while (GetTickCount() < end)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        Sleep(10);
    }

    g_asioRunning = false;
    Sleep(50);
    g_asio->stop();
    g_asio->disposeBuffers();

    uint64_t switches = g_asioBufferSwitches.load();
    printf("\n==== ASIO OUTPUT VERDICT ====\n");
    printf("driver init + createBuffers + start:   OK\n");
    printf("bufferSwitch callbacks fired:          %llu (expect ~%.0f)\n",
           (unsigned long long) switches, (double) seconds * g_sampleRate / g_bufSize);
    bool streamed = switches > 10;
    printf("DAC streaming callbacks flowing:       %s\n", streamed ? "YES" : "NO");

    if (capturePid)
    {
        g_capStop = true;
        if (capThread.joinable()) capThread.join();
        printf("\n==== COEXISTENCE (capture ran during ASIO output) ====\n");
        printf("process-loopback capture peak:         %.4f (%llu frames)\n",
               g_capPeak.load(), (unsigned long long) g_capFrames.load());
        printf("both subsystems live simultaneously:   %s\n",
               (streamed && g_capFrames.load() > 0) ? "YES — capture + ASIO output coexist" : "NO");
    }

    g_asio->Release();
    CoUninitialize();
    printf("\n>>> ASIO OUTPUT PATH: %s <<<\n",
           streamed ? "VIABLE — Plugin Play can drive the external DAC via ASIO"
                    : "PROBLEM — see flags above");
    return streamed ? 0 : 3;
}

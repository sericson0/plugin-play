// M2 experiment (part 4, req #3): does routing DJ audio through WASAPI shared
// (so we can process-loopback capture it) degrade sound quality vs. going
// straight to ASIO?
//
// Method (all in one process):
//   - Render a precise 1000 Hz sine at 0.5 (-6.02 dBFS peak) through a SHARED
//     WASAPI render stream on the default endpoint.
//   - Simultaneously process-loopback capture THIS process (float @ endpoint mix rate).
//   - Analyse a clean 1-second window: fundamental level (Goertzel@1000) + THD+N.
//
// Two trials:
//   A) render rate == endpoint mix rate  -> no sample-rate conversion (the ideal case)
//   B) render rate != mix rate           -> forces Windows' shared-engine resampler
//      (AUTOCONVERTPCM), i.e. the 44.1-track-into-48k-endpoint situation.
//
// Interpretation: if A is level-accurate with THD+N near the float noise floor,
// the shared path itself is transparent. B isolates the ONE real quality lever:
// sample-rate conversion, which you avoid by matching rates end to end.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmreg.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::FtmBase;

static const double kToneHz = 1000.0;
static const double kAmp    = 0.5;   // -6.02 dBFS peak
static const double kPI     = 3.14159265358979323846;

#ifndef AUDCLNT_STREAMFLAGS_RAW
#define AUDCLNT_STREAMFLAGS_RAW 0x00010000
#endif

static const GUID kFloatSub =
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

// ------------------------------------------------ process-loopback activation
class ActHandler
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
                          IActivateAudioInterfaceCompletionHandler>
{
public:
    HANDLE               done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT              hr   = E_FAIL;
    ComPtr<IAudioClient> client;
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* op) override
    {
        IUnknown* u = nullptr; op->GetActivateResult(&hr, &u);
        if (SUCCEEDED(hr) && u) { u->QueryInterface(IID_PPV_ARGS(&client)); u->Release(); }
        SetEvent(done); return S_OK;
    }
};

static void makeFloatFormat(WAVEFORMATEXTENSIBLE& w, int channels, int rate)
{
    w = {};
    w.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    w.Format.nChannels       = (WORD) channels;
    w.Format.nSamplesPerSec  = (DWORD) rate;
    w.Format.wBitsPerSample  = 32;
    w.Format.nBlockAlign     = (WORD) (channels * 4);
    w.Format.nAvgBytesPerSec = rate * channels * 4;
    w.Format.cbSize          = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    w.Samples.wValidBitsPerSample = 32;
    w.dwChannelMask          = (channels == 1) ? SPEAKER_FRONT_CENTER
                                               : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    w.SubFormat              = kFloatSub;
}

// Goertzel peak amplitude of frequency f0 over N samples at rate fs.
static double goertzelPeak(const std::vector<float>& x, size_t off, int N, double f0, double fs)
{
    double re = 0.0, im = 0.0;
    for (int n = 0; n < N; ++n)
    {
        double ph = 2.0 * kPI * f0 * n / fs;
        re += x[off + n] * cos(ph);
        im -= x[off + n] * sin(ph);
    }
    return 2.0 * sqrt(re * re + im * im) / N;   // peak amplitude of that component
}

static double db(double lin) { return 20.0 * log10(lin > 1e-12 ? lin : 1e-12); }

struct Result { double fundDbfs; double thdPlusNdb; double thdDb; double peak; int discont; size_t frames; bool ok; };

static bool g_raw = false;   // bypass APO / audio enhancements on both streams

static Result runTrial(IMMDevice* dev, const WAVEFORMATEX* mix, int renderRate, bool forceSrc)
{
    Result r { 0, 0, 0, 0, 0, 0, false };
    int ch      = mix->nChannels;
    int mixRate = (int) mix->nSamplesPerSec;
    DWORD rawFlag = g_raw ? AUDCLNT_STREAMFLAGS_RAW : 0;

    // ---- render client (shared), optionally with sample-rate conversion ----
    ComPtr<IAudioClient> rc;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**) rc.GetAddressOf())))
        return r;
    WAVEFORMATEXTENSIBLE rfmt; makeFloatFormat(rfmt, ch, renderRate);
    DWORD rflags = rawFlag | (forceSrc ? (AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY) : 0);
    if (FAILED(rc->Initialize(AUDCLNT_SHAREMODE_SHARED, rflags, 5000000, 0, (WAVEFORMATEX*) &rfmt, nullptr)))
    { fprintf(stderr, "  render Initialize failed (rate %d)\n", renderRate); return r; }
    UINT32 rBuf = 0; rc->GetBufferSize(&rBuf);
    ComPtr<IAudioRenderClient> render;
    if (FAILED(rc->GetService(IID_PPV_ARGS(&render)))) return r;

    // ---- capture: process loopback on our own PID, float @ mix rate ----
    AUDIOCLIENT_ACTIVATION_PARAMS ap {};
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
    ap.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT pv {}; pv.vt = VT_BLOB; pv.blob.cbSize = sizeof(ap); pv.blob.pBlobData = (BYTE*) &ap;
    auto h = Microsoft::WRL::Make<ActHandler>();
    ComPtr<IActivateAudioInterfaceAsyncOperation> op;
    if (FAILED(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                           __uuidof(IAudioClient), &pv, h.Get(), &op))) return r;
    WaitForSingleObject(h->done, INFINITE);
    if (FAILED(h->hr)) return r;
    WAVEFORMATEXTENSIBLE cfmt; makeFloatFormat(cfmt, ch, mixRate);
    if (FAILED(h->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
              rawFlag | AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
              5000000, 0, (WAVEFORMATEX*) &cfmt, nullptr))) return r;
    HANDLE cev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    h->client->SetEventHandle(cev);
    ComPtr<IAudioCaptureClient> cap;
    if (FAILED(h->client->GetService(IID_PPV_ARGS(&cap)))) return r;

    // ---- prime render buffer, then start both ----
    double phase = 0.0, inc = 2.0 * kPI * kToneHz / renderRate;
    auto fill = [&](UINT32 frames)
    {
        BYTE* p = nullptr;
        if (FAILED(render->GetBuffer(frames, &p))) return;
        float* f = (float*) p;
        for (UINT32 i = 0; i < frames; ++i)
        {
            float v = (float) (kAmp * sin(phase));
            phase += inc; if (phase > 2.0 * kPI) phase -= 2.0 * kPI;
            for (int c = 0; c < ch; ++c) f[i * ch + c] = v;
        }
        render->ReleaseBuffer(frames, 0);
    };
    fill(rBuf);
    rc->Start();
    h->client->Start();

    std::vector<float> mono;               // captured channel 0
    mono.reserve((size_t) mixRate * 3);
    int discont = 0;
    DWORD end = GetTickCount() + 1800;      // ~1.8 s
    while (GetTickCount() < end)
    {
        UINT32 pad = 0;
        if (SUCCEEDED(rc->GetCurrentPadding(&pad)) && rBuf > pad) fill(rBuf - pad);
        UINT32 pkt = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&pkt)) && pkt > 0)
        {
            BYTE* d; UINT32 n; DWORD fl;
            if (FAILED(cap->GetBuffer(&d, &n, &fl, nullptr, nullptr))) break;
            if (fl & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) discont++;
            const float* s = (const float*) d;
            if (! (fl & AUDCLNT_BUFFERFLAGS_SILENT))
                for (UINT32 i = 0; i < n; ++i) mono.push_back(s[i * ch]);
            else
                for (UINT32 i = 0; i < n; ++i) mono.push_back(0.0f);
            cap->ReleaseBuffer(n);
        }
        Sleep(3);
    }
    rc->Stop(); h->client->Stop();
    r.discont = discont;
    r.frames  = mono.size();

    // ---- analyse a clean 1-second window from the middle ----
    int N = mixRate;                        // exactly 1 s -> 1000 Hz = 1000 whole cycles
    if ((int) mono.size() < N + mixRate / 2) { fprintf(stderr, "  too few captured samples (%zu)\n", mono.size()); return r; }
    size_t off = mono.size() / 2 - N / 2;

    double fund = goertzelPeak(mono, off, N, kToneHz, mixRate);
    double sumSq = 0.0, peak = 0.0;
    for (int n = 0; n < N; ++n) { double v = mono[off + n]; sumSq += v * v; if (fabs(v) > peak) peak = fabs(v); }
    double totalRms = sqrt(sumSq / N);
    r.peak = peak;
    double fundRms  = fund / sqrt(2.0);

    double harmSq = 0.0;
    for (int hmul = 2; hmul <= 5; ++hmul)
    {
        double a = goertzelPeak(mono, off, N, kToneHz * hmul, mixRate);
        harmSq += (a / sqrt(2.0)) * (a / sqrt(2.0));
    }
    double resid = sqrt(fmax(0.0, totalRms * totalRms - fundRms * fundRms));

    r.fundDbfs   = db(fund);
    r.thdPlusNdb = db(resid / fundRms);
    r.thdDb      = db(sqrt(harmSq) / fundRms);
    r.ok         = true;
    return r;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "raw") == 0 || strcmp(argv[i], "--raw") == 0) g_raw = true;

    std::string wantEp;   // endpoint name substring; empty = default
    bool listEps = false;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "list") == 0) listEps = true;
        else if (strncmp(argv[i], "ep=", 3) == 0) wantEp = argv[i] + 3;
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) { fprintf(stderr, "CoInit failed\n"); return 2; }

    ComPtr<IMMDeviceEnumerator> en;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));

    auto nameOf = [](IMMDevice* d) -> std::string {
        std::string s = "(unknown)";
        ComPtr<IPropertyStore> ps;
        if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)))
        {
            PROPVARIANT v; PropVariantInit(&v);
            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
            { char b[256]; WideCharToMultiByte(CP_UTF8,0,v.pwszVal,-1,b,sizeof(b),nullptr,nullptr); s=b; }
            PropVariantClear(&v);
        }
        return s;
    };

    if (listEps)
    {
        ComPtr<IMMDeviceCollection> coll;
        en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll);
        UINT c = 0; if (coll) coll->GetCount(&c);
        printf("Active render endpoints (%u):\n", c);
        for (UINT i = 0; i < c; ++i)
        { ComPtr<IMMDevice> d; coll->Item(i, &d); if (d) printf("  - %s\n", nameOf(d.Get()).c_str()); }
        return 0;
    }

    ComPtr<IMMDevice> dev;
    if (! wantEp.empty())
    {
        ComPtr<IMMDeviceCollection> coll;
        en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll);
        UINT c = 0; if (coll) coll->GetCount(&c);
        std::string lw = wantEp; for (auto& ch : lw) ch = (char) tolower((unsigned char) ch);
        for (UINT i = 0; i < c && ! dev; ++i)
        {
            ComPtr<IMMDevice> d; coll->Item(i, &d);
            std::string n = nameOf(d.Get()), ln = n;
            for (auto& ch : ln) ch = (char) tolower((unsigned char) ch);
            if (ln.find(lw) != std::string::npos) dev = d;
        }
        if (! dev) { fprintf(stderr, "No active render endpoint matching \"%s\"\n", wantEp.c_str()); return 2; }
    }
    else if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
    { fprintf(stderr, "no default render endpoint\n"); return 2; }

    // which endpoint are we on?
    std::string endpointName = "(unknown)";
    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)))
    {
        PROPVARIANT v; PropVariantInit(&v);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
        {
            char buf[256]; WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, buf, sizeof(buf), nullptr, nullptr);
            endpointName = buf;
        }
        PropVariantClear(&v);
    }

    ComPtr<IAudioClient> probe;
    dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**) probe.GetAddressOf());
    WAVEFORMATEX* mix = nullptr;
    probe->GetMixFormat(&mix);
    int mixRate = (int) mix->nSamplesPerSec;

    printf("Default endpoint: %s\n", endpointName.c_str());
    printf("Mode: %s\n", g_raw ? "RAW (APO / audio-enhancements bypassed)" : "normal (APOs active)");
    printf("Mix format: %d Hz, %d ch, %d-bit%s\n\n",
           mixRate, mix->nChannels, mix->wBitsPerSample,
           (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) ? " (extensible)" : "");
    printf("Reference tone: %.0f Hz @ %.4f (%.2f dBFS peak)\n\n", kToneHz, kAmp, db(kAmp));

    int otherRate = (mixRate == 48000) ? 44100 : 48000;

    printf("Trial A: render @ %d Hz == mix rate (NO sample-rate conversion)\n", mixRate);
    Result a = runTrial(dev.Get(), mix, mixRate, false);
    if (a.ok)
        printf("  fundamental: %+.2f dBFS   THD+N: %.1f dB   THD: %.1f dB\n"
               "  raw peak sample: %.4f   discontinuities: %d   captured frames: %zu\n\n",
               a.fundDbfs, a.thdPlusNdb, a.thdDb, a.peak, a.discont, a.frames);

    printf("Trial B: render @ %d Hz -> %d Hz endpoint (Windows shared-engine RESAMPLE)\n", otherRate, mixRate);
    Result b = runTrial(dev.Get(), mix, otherRate, true);
    if (b.ok)
        printf("  fundamental: %+.2f dBFS   THD+N: %.1f dB   THD: %.1f dB\n"
               "  raw peak sample: %.4f   discontinuities: %d   captured frames: %zu\n\n",
               b.fundDbfs, b.thdPlusNdb, b.thdDb, b.peak, b.discont, b.frames);

    printf("==== SOUND QUALITY VERDICT ====\n");
    if (a.ok)
    {
        bool levelOk = fabs(a.fundDbfs - db(kAmp)) < 0.1;     // within 0.1 dB of source
        bool cleanA  = a.thdPlusNdb < -80.0;                  // near float noise floor
        printf("matched-rate level accurate (<0.1 dB): %s (%.2f vs %.2f dBFS)\n",
               levelOk ? "YES" : "NO", a.fundDbfs, db(kAmp));
        printf("matched-rate THD+N near noise floor:   %s (%.1f dB)\n",
               cleanA ? "YES" : "NO", a.thdPlusNdb);
        printf(">>> Shared-path with MATCHED rates is %s <<<\n",
               (levelOk && cleanA) ? "TRANSPARENT — no quality loss vs direct ASIO"
                                   : "SUSPECT — investigate (enhancements? volume<100%?)");
    }
    if (a.ok && b.ok)
        printf("Resampling cost (mismatched rate) raises THD+N by %.1f dB -> keep the whole\n"
               "chain at the track rate (usually 44.1k) to avoid it.\n",
               b.thdPlusNdb - a.thdPlusNdb);

    CoTaskMemFree(mix);
    // no CoUninitialize() — avoid releasing ComPtrs after COM teardown
    return a.ok ? 0 : 3;
}

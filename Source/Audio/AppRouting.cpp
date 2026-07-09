#include "AppRouting.h"

#if JUCE_WINDOWS

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <tlhelp32.h>
#include <thread>
#include <vector>

// RoGetActivationFactory / WindowsCreateString live in the WinRT core (combase);
// the import lib is runtimeobject.lib. The rest of WASAPI/COM is already linked.
#pragma comment (lib, "runtimeobject.lib")

namespace play::AppRouting
{
using Microsoft::WRL::ComPtr;

//==============================================================================
// IAudioPolicyConfig — the undocumented interface behind Windows' "App volume and
// device preferences". Layout taken verbatim from EarTrumpet (21H2+ variant). It
// derives from IInspectable, then carries 19 methods we never call, then the three
// we do. The interface IID + activation string are what matter; the placeholder
// slot COUNT must be exact or the vtable offsets are wrong.
//
// NOTE: this is the 21H2+ (Windows 11 / recent Win10) layout. Older Windows 10
// builds expose a different variant (IID 2a59116d-6c4f-45e0-a74f-707e3fef9258) with
// a different slot count — not implemented here; isSupported() returns false there
// and the caller falls back to the manual cable workflow.
MIDL_INTERFACE("ab3d4648-e242-459f-b02f-541c70306324")
IAudioPolicyConfig21H2 : public IInspectable
{
public:
    virtual HRESULT STDMETHODCALLTYPE incomplete_01() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_02() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_03() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_04() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_05() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_06() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_07() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_08() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_09() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_10() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_11() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_12() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_13() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_14() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_15() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_16() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_17() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_18() = 0;
    virtual HRESULT STDMETHODCALLTYPE incomplete_19() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPersistedDefaultAudioEndpoint (
        DWORD processId, EDataFlow flow, ERole role, HSTRING deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPersistedDefaultAudioEndpoint (
        DWORD processId, EDataFlow flow, ERole role, HSTRING* deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearAllPersistedApplicationDefaultEndpoints() = 0;
};

// The persisted device-id is not the raw MMDevice id but a "software device" path
// wrapping it, with a render/capture interface GUID suffix (also from EarTrumpet).
static const wchar_t* kMmdevapiToken = L"\\\\?\\SWD#MMDEVAPI#";
static const wchar_t* kRenderSuffix  = L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";

//==============================================================================
// RoGetActivationFactory needs a WinRT-initialised apartment. JUCE's message thread
// is an STA brought up with plain CoInitializeEx (no WinRT), where the factory
// activation fails. So every routing call runs its COM/WinRT work on a short-lived
// worker thread that we bring up as an MTA via RoInitialize — a clean apartment
// regardless of who called us. WinRT objects have apartment affinity, so each call
// both obtains and uses the factory within the same worker.
template <typename Fn>
static auto runOnMta (Fn&& fn) -> decltype (fn())
{
    using Result = decltype (fn());
    Result result {};

    std::thread worker ([&]
    {
        const bool ok = SUCCEEDED (RoInitialize (RO_INIT_MULTITHREADED));
        result = fn();
        if (ok)
            RoUninitialize();
    });
    worker.join();

    return result;
}

static ComPtr<IAudioPolicyConfig21H2> getPolicyConfig()
{
    HSTRING className = nullptr;
    const wchar_t* name = L"Windows.Media.Internal.AudioPolicyConfig";
    if (FAILED (WindowsCreateString (name, (UINT32) wcslen (name), &className)))
        return nullptr;

    ComPtr<IAudioPolicyConfig21H2> cfg;
    HRESULT hr = RoGetActivationFactory (className, __uuidof (IAudioPolicyConfig21H2),
                                         (void**) cfg.GetAddressOf());
    WindowsDeleteString (className);
    return SUCCEEDED (hr) ? cfg : nullptr;
}

static juce::String friendlyName (IMMDevice* device)
{
    juce::String result;
    ComPtr<IPropertyStore> props;
    if (SUCCEEDED (device->OpenPropertyStore (STGM_READ, props.GetAddressOf())))
    {
        PROPVARIANT v; PropVariantInit (&v);
        if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
            result = juce::String (v.pwszVal);
        PropVariantClear (&v);
    }
    return result;
}

struct Endpoint
{
    juce::String id;
    juce::String name;
    juce::String instanceKey;   // shared by a single cable's input + output endpoints
};

// A cable's playback ("CABLE Input …") and recording ("CABLE Output …") endpoints
// share the same parenthetical driver name — e.g. both end in "(VB-Audio Virtual
// Cable)". Keying on that lets us pair the two sides of the SAME cable even when
// several cables (VB-CABLE A+B, multiple installs) are present, instead of blindly
// taking the first "in" and first "out" which can come from different cables.
static juce::String cableInstanceKey (const juce::String& name)
{
    const int open  = name.lastIndexOfChar ('(');
    const int close = name.lastIndexOfChar (')');
    if (open >= 0 && close > open)
        return name.substring (open + 1, close).trim().toLowerCase();

    return name.trim().toLowerCase();
}

// All active endpoints of the given flow whose friendly name looks like a virtual
// cable (contains "cable"). Each carries its raw id and instance key.
static std::vector<Endpoint> collectCableEndpoints (EDataFlow flow)
{
    std::vector<Endpoint> out;

    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS (en.GetAddressOf()))))
        return out;

    ComPtr<IMMDeviceCollection> coll;
    if (FAILED (en->EnumAudioEndpoints (flow, DEVICE_STATE_ACTIVE, coll.GetAddressOf())))
        return out;

    UINT count = 0;
    coll->GetCount (&count);

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        if (FAILED (coll->Item (i, device.GetAddressOf())))
            continue;

        const auto name = friendlyName (device.Get());
        if (! name.containsIgnoreCase ("cable"))
            continue;

        LPWSTR id = nullptr;
        if (SUCCEEDED (device->GetId (&id)) && id != nullptr)
        {
            out.push_back ({ juce::String (id), name, cableInstanceKey (name) });
            CoTaskMemFree (id);
        }
    }

    return out;
}

//==============================================================================
bool isSupported()
{
    return runOnMta ([] { return getPolicyConfig() != nullptr; });
}

bool findCable (CablePair& out)
{
    return runOnMta ([&]
    {
        // A cable's playback endpoint ("CABLE Input …" / "CABLE In 16 Ch …") is a
        // render device; its recording endpoint ("CABLE Output …") is a capture
        // device. Collect both sides, then pair by shared instance so we don't wire
        // audio into one cable and try to read it back off another.
        const auto renders  = collectCableEndpoints (eRender);
        const auto captures = collectCableEndpoints (eCapture);

        if (renders.empty() || captures.empty())
            return false;

        auto looksLikeInput  = [] (const juce::String& n) { return n.containsIgnoreCase ("in"); };
        auto looksLikeOutput = [] (const juce::String& n) { return n.containsIgnoreCase ("out"); };

        const Endpoint* bestRender  = nullptr;
        const Endpoint* bestCapture = nullptr;

        // Best: a render+capture pair from the same cable instance, with the expected
        // in/out naming. Fall back to any same-instance pair, then to first-of-each.
        for (const auto& r : renders)
        {
            for (const auto& c : captures)
            {
                if (r.instanceKey != c.instanceKey)
                    continue;

                if (looksLikeInput (r.name) && looksLikeOutput (c.name))
                {
                    bestRender = &r; bestCapture = &c;
                    break;
                }

                if (bestRender == nullptr) { bestRender = &r; bestCapture = &c; }
            }

            if (bestRender != nullptr && looksLikeInput (bestRender->name))
                break;
        }

        if (bestRender == nullptr) { bestRender = &renders.front(); bestCapture = &captures.front(); }

        out.renderDeviceId = bestRender->id;
        out.renderName     = bestRender->name;
        out.captureName    = bestCapture->name;
        return true;
    });
}

bool routeAppOutput (juce::uint32 pid, const juce::String& renderDeviceId)
{
    if (renderDeviceId.isEmpty())
        return false;

    return runOnMta ([&]
    {
        auto cfg = getPolicyConfig();
        if (cfg == nullptr)
            return false;

        const auto swd = juce::String (kMmdevapiToken) + renderDeviceId + juce::String (kRenderSuffix);

        HSTRING dev = nullptr;
        const auto* swdW = swd.toWideCharPointer();
        if (FAILED (WindowsCreateString (swdW, (UINT32) wcslen (swdW), &dev)))
            return false;

        HRESULT a = cfg->SetPersistedDefaultAudioEndpoint ((DWORD) pid, eRender, eConsole,    dev);
        HRESULT b = cfg->SetPersistedDefaultAudioEndpoint ((DWORD) pid, eRender, eMultimedia, dev);

        WindowsDeleteString (dev);
        return SUCCEEDED (a) && SUCCEEDED (b);
    });
}

bool clearAppOutput (juce::uint32 pid)
{
    return runOnMta ([&]
    {
        auto cfg = getPolicyConfig();
        if (cfg == nullptr)
            return false;

        // A null HSTRING removes the per-app override, restoring the system default.
        HRESULT a = cfg->SetPersistedDefaultAudioEndpoint ((DWORD) pid, eRender, eConsole,    nullptr);
        HRESULT b = cfg->SetPersistedDefaultAudioEndpoint ((DWORD) pid, eRender, eMultimedia, nullptr);
        return SUCCEEDED (a) && SUCCEEDED (b);
    });
}

static std::vector<DWORD> processIdsForExe (const juce::String& exe)
{
    std::vector<DWORD> pids;

    HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return pids;

    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof (entry);

    if (Process32FirstW (snapshot, &entry))
    {
        do
        {
            if (exe.equalsIgnoreCase (juce::String (entry.szExeFile)))
                pids.push_back (entry.th32ProcessID);
        }
        while (Process32NextW (snapshot, &entry));
    }

    CloseHandle (snapshot);
    return pids;
}

bool clearAppOutputByName (const juce::String& exe)
{
    if (exe.isEmpty())
        return false;

    const auto pids = processIdsForExe (exe);
    if (pids.empty())
        return false;

    return runOnMta ([&]
    {
        auto cfg = getPolicyConfig();
        if (cfg == nullptr)
            return false;

        bool any = false;
        for (auto pid : pids)
        {
            HRESULT a = cfg->SetPersistedDefaultAudioEndpoint (pid, eRender, eConsole,    nullptr);
            HRESULT b = cfg->SetPersistedDefaultAudioEndpoint (pid, eRender, eMultimedia, nullptr);
            any = any || (SUCCEEDED (a) && SUCCEEDED (b));
        }
        return any;
    });
}

bool clearAllOverrides()
{
    return runOnMta ([&]
    {
        auto cfg = getPolicyConfig();
        if (cfg == nullptr)
            return false;

        return (bool) SUCCEEDED (cfg->ClearAllPersistedApplicationDefaultEndpoints());
    });
}

bool isProcessRunning (juce::uint32 pid)
{
    if (pid == 0)
        return false;

    // Plain Win32 (no WinRT), so no MTA worker needed. QUERY_LIMITED_INFORMATION is
    // accessible across integrity levels; WaitForSingleObject(0) distinguishes a live
    // process (WAIT_TIMEOUT) from an exited one (WAIT_OBJECT_0) without the STILL_ACTIVE
    // exit-code ambiguity.
    HANDLE h = OpenProcess (SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid);
    if (h == nullptr)
        return false;

    const DWORD state = WaitForSingleObject (h, 0);
    CloseHandle (h);
    return state == WAIT_TIMEOUT;
}

} // namespace play::AppRouting

#else //=========================================================================

namespace play::AppRouting
{
    bool isSupported()                                        { return false; }
    bool findCable (CablePair&)                               { return false; }
    bool routeAppOutput (juce::uint32, const juce::String&)   { return false; }
    bool clearAppOutput (juce::uint32)                        { return false; }
    bool clearAppOutputByName (const juce::String&)           { return false; }
    bool clearAllOverrides()                                  { return false; }
    bool isProcessRunning (juce::uint32)                      { return false; }
}

#endif

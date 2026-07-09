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
#include <thread>

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

// Returns the first active endpoint of the given flow whose friendly name contains
// `needle` (case-insensitive), with its raw id and name.
static bool findEndpoint (EDataFlow flow, const juce::String& needle,
                          juce::String& idOut, juce::String& nameOut)
{
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS (en.GetAddressOf()))))
        return false;

    ComPtr<IMMDeviceCollection> coll;
    if (FAILED (en->EnumAudioEndpoints (flow, DEVICE_STATE_ACTIVE, coll.GetAddressOf())))
        return false;

    UINT count = 0;
    coll->GetCount (&count);

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        if (FAILED (coll->Item (i, device.GetAddressOf())))
            continue;

        const auto name = friendlyName (device.Get());
        if (name.containsIgnoreCase (needle))
        {
            LPWSTR id = nullptr;
            if (SUCCEEDED (device->GetId (&id)) && id != nullptr)
            {
                idOut   = juce::String (id);
                nameOut = name;
                CoTaskMemFree (id);
                return true;
            }
        }
    }

    return false;
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
        juce::String renderId, renderName, captureId, captureName;

        // VB-CABLE exposes "CABLE Input*" (playback) paired with "CABLE Output*"
        // (recording). Match the "in"/"out" tokens so renamed variants (e.g. the
        // user's "CABLE In 16 Ch") still pair up.
        const bool haveRender  = findEndpoint (eRender,  "cable in",  renderId,  renderName);
        const bool haveCapture = findEndpoint (eCapture, "cable out", captureId, captureName);

        if (! (haveRender && haveCapture))
            return false;

        out.renderDeviceId = renderId;
        out.renderName     = renderName;
        out.captureName    = captureName;
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
        if (FAILED (WindowsCreateString (swd.toWideCharPointer(), (UINT32) swd.length(), &dev)))
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

} // namespace play::AppRouting

#else //=========================================================================

namespace play::AppRouting
{
    bool isSupported()                                        { return false; }
    bool findCable (CablePair&)                               { return false; }
    bool routeAppOutput (juce::uint32, const juce::String&)   { return false; }
    bool clearAppOutput (juce::uint32)                        { return false; }
}

#endif

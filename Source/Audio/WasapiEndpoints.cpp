#include "WasapiEndpoints.h"

#if JUCE_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>

namespace play::WasapiEndpoints
{
using Microsoft::WRL::ComPtr;

// PKEY_Device_FriendlyName, defined locally so we don't need INITGUID (which would
// pull in every other GUID definition) or an extra Windows lib on the link line.
static const PROPERTYKEY kFriendlyName =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

// JUCE already initialises COM on the message thread (as an STA), so our
// CoInitializeEx here usually returns RPC_E_CHANGED_MODE — that's fine, COM is
// live and we just avoid an unbalanced CoUninitialize.
struct ScopedCom
{
    ScopedCom()  { auto hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED); ownsInit = SUCCEEDED (hr); }
    ~ScopedCom() { if (ownsInit) CoUninitialize(); }
    bool ownsInit = false;
};

double mixRateForDevice (const juce::String& deviceName, bool isInput)
{
    if (deviceName.isEmpty())
        return 0.0;

    ScopedCom com;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS (enumerator.GetAddressOf()))))
        return 0.0;

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED (enumerator->EnumAudioEndpoints (isInput ? eCapture : eRender,
                                                DEVICE_STATE_ACTIVE, collection.GetAddressOf())))
        return 0.0;

    UINT count = 0;
    collection->GetCount (&count);

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        if (FAILED (collection->Item (i, device.GetAddressOf())))
            continue;

        ComPtr<IPropertyStore> props;
        if (FAILED (device->OpenPropertyStore (STGM_READ, props.GetAddressOf())))
            continue;

        PROPVARIANT name;
        PropVariantInit (&name);
        juce::String friendly;
        if (SUCCEEDED (props->GetValue (kFriendlyName, &name))
            && name.vt == VT_LPWSTR && name.pwszVal != nullptr)
            friendly = juce::String (name.pwszVal);
        PropVariantClear (&name);

        // JUCE's WASAPI device names are the endpoint friendly names, so this
        // matches the string shown in our input/output selectors.
        if (! friendly.equalsIgnoreCase (deviceName))
            continue;

        ComPtr<IAudioClient> client;
        if (FAILED (device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr,
                                      (void**) client.GetAddressOf())))
            return 0.0;

        WAVEFORMATEX* mix = nullptr;
        if (FAILED (client->GetMixFormat (&mix)) || mix == nullptr)
            return 0.0;

        const double rate = (double) mix->nSamplesPerSec;
        CoTaskMemFree (mix);
        return rate;
    }

    return 0.0;
}
} // namespace play::WasapiEndpoints

#else

namespace play::WasapiEndpoints
{
    double mixRateForDevice (const juce::String&, bool) { return 0.0; }
}

#endif

#pragma once

#include <JuceHeader.h>

namespace play::AppRouting
{
    //==============================================================================
    /** A virtual cable's playback + recording endpoint pair. Audio sent to the
        `renderDeviceId` playback endpoint appears on the `captureName` recording
        endpoint, which Plugin Play reads as its input. */
    struct CablePair
    {
        juce::String renderDeviceId;   // raw IMMDevice id of the cable's playback endpoint
        juce::String renderName;       // that endpoint's friendly name (for display/logging)
        juce::String captureName;      // friendly name of the cable's recording endpoint
                                       // (matches the JUCE input-device name to select)

        bool isValid() const { return renderDeviceId.isNotEmpty() && captureName.isNotEmpty(); }
    };

    /** True if per-app output routing is available on this OS — i.e. the
        undocumented IAudioPolicyConfig factory could be bound. */
    bool isSupported();

    /** Finds an installed virtual cable's playback+recording pair, or returns false
        if none is present. VB-CABLE naming ("CABLE In*" / "CABLE Out*") is targeted.
        Windows-only; always false elsewhere. */
    bool findCable (CablePair& out);

    /** Redirects the given process's audio OUTPUT to the render endpoint with the
        given raw IMMDevice id, for both the eConsole and eMultimedia roles. Returns
        false on failure (or unsupported OS). The app keeps producing audio — it just
        renders to the new endpoint, so process capture / cable loopback still sees it. */
    bool routeAppOutput (juce::uint32 pid, const juce::String& renderDeviceId);

    /** Clears a process's output override, restoring it to the system default. */
    bool clearAppOutput (juce::uint32 pid);
}

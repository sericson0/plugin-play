#pragma once

#include <JuceHeader.h>

namespace play
{

//==============================================================================
/**
    Virtual-cable support for the "device" routing model, where the DJ software
    selects a cable as its output and Plugin Play reads the cable as its input
    (the alternative to the default driverless process-loopback capture).

    We do not bundle a driver — VB-CABLE's licence forbids redistribution without
    a VB-Audio agreement, and no attestation-signed open driver exists yet — so
    this is a *guided* install. To keep it easy we fetch VB-Audio's own unmodified
    installer from their server at the user's request (not redistribution: the
    bytes come straight from VB-Audio) and launch it elevated.
*/
namespace VirtualCable
{
    /** Official VB-CABLE download page — also the manual fallback and version source. */
    inline const juce::String downloadPage { "https://vb-audio.com/Cable/" };

    /** Direct zip used only if scraping the download page for the latest pack fails. */
    inline const juce::String fallbackZipUrl {
        "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip" };

    /** VB-CABLE is donationware; point users here to pay what they can. */
    inline const juce::String donateUrl {
        "https://shop.vb-audio.com/en/win-apps/11-vb-cable.html" };

    /** Name of the elevated installer inside the VB-CABLE zip (64-bit Windows). */
    inline const juce::String installerExe { "VBCABLE_Setup_x64.exe" };

    /** True if a device name looks like a known virtual audio cable. */
    bool nameLooksLikeCable (const juce::String& deviceName);

    /** Scans all audio device types for an installed virtual cable.
        Returns the matched device name, or an empty string if none is found. */
    juce::String findInstalled (juce::AudioDeviceManager& deviceManager);

    /** Reads the download page and returns the URL of the newest VBCABLE_Driver_PackN.zip.
        Falls back to fallbackZipUrl if the page can't be read or parsed. Blocking — call
        off the message thread. */
    juce::String resolveLatestZipUrl();

    /** Downloads url to dest, overwriting. Blocking; returns false on any failure. */
    bool downloadTo (const juce::String& url, const juce::File& dest);

    /** Launches the installer with a UAC elevation prompt. Returns false if the user
        declines elevation or the launch fails. */
    bool launchInstaller (const juce::File& exe);

    /** Restarts Windows immediately (via shutdown.exe). Returns false on non-Windows
        or if the restart couldn't be started. */
    bool reboot();
}

//==============================================================================
/** Guided VB-CABLE install / configuration walkthrough, shown in a dialog.

    The primary action downloads VB-Audio's installer, extracts it, and launches it
    elevated so a non-technical DJ only has to approve the prompts and reboot. A
    manual "open download page" link stays available as a fallback. */
class CableSetupComponent : public juce::Component
{
public:
    explicit CableSetupComponent (juce::AudioDeviceManager& deviceManager);
    ~CableSetupComponent() override;

    /** Opens the walkthrough as an async dialog styled like the rest of the app. */
    static void launch (juce::AudioDeviceManager& deviceManager);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct InstallThread;
    friend struct InstallThread;

    void recheck();
    void refreshContent();

    void startInstall();
    void setBusy (bool busy);
    void showProgress (const juce::String& message);   // called on the message thread
    void installFailed (const juce::String& message);   // called on the message thread
    void installLaunched();                             // called on the message thread
    void confirmAndReboot();

    juce::AudioDeviceManager& deviceManager;

    juce::Label           heading;
    juce::Label           statusLabel;
    juce::TextEditor      body;
    juce::TextButton      installButton { "DOWNLOAD & INSTALL VB-CABLE" };
    juce::TextButton      rebootButton  { "REBOOT NOW TO FINISH" };
    juce::TextButton      recheckButton { "RE-CHECK" };
    juce::TextButton      closeButton   { "CLOSE" };
    juce::HyperlinkButton pageLink   { "Open download page instead",
                                       juce::URL (VirtualCable::downloadPage) };
    juce::HyperlinkButton donateLink { "VB-CABLE is donationware - please support VB-Audio",
                                       juce::URL (VirtualCable::donateUrl) };

    juce::String detectedCable;
    bool busy = false;

    std::unique_ptr<InstallThread> installThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CableSetupComponent)
};

} // namespace play

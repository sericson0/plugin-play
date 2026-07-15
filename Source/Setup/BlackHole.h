#pragma once

#include <JuceHeader.h>

#if JUCE_MAC

namespace play
{

//==============================================================================
/**
    BlackHole virtual-device support — the macOS analog of the Windows VB-CABLE
    path, used on macOS 11–14.3 where Core Audio process taps aren't available
    (see ProcessTap). The user routes their source app (or the system) into
    BlackHole and Plugin Play reads BlackHole as its input, processes, and plays
    out to real speakers.

    BlackHole (github.com/ExistentialAudio/BlackHole, GPL-3.0) is not bundled: it
    installs a system audio driver that needs admin rights, and its direct
    download is gated behind an email signup. So this is a *guided* setup —
    detect, point the user at Homebrew or the download page, and re-check.
*/
namespace BlackHole
{
    /** BlackHole download page (also the manual install fallback). */
    inline const juce::String downloadPage { "https://existential.audio/blackhole/" };

    /** BlackHole source / releases (GPL-3.0), for users who prefer a direct .pkg. */
    inline const juce::String githubReleases { "https://github.com/ExistentialAudio/BlackHole/releases" };

    /** One-line install for anyone who has Homebrew — the cleanest path. The 2ch
        variant is all a stereo DJ setup needs. */
    inline const juce::String homebrewCommand { "brew install blackhole-2ch" };

    /** True if a device name looks like a BlackHole (or other common Mac virtual
        audio device) endpoint. */
    bool nameLooksLikeVirtualDevice (const juce::String& deviceName);

    /** Looks through all audio device types for an installed BlackHole/virtual
        device. Returns the matched device name, or "" if none is found. With
        rescan true it forces a fresh driver scan first (can block briefly). */
    juce::String findInstalled (juce::AudioDeviceManager& deviceManager, bool rescan);
}

//==============================================================================
/** Guided BlackHole install / routing walkthrough, shown in a dialog styled like
    the rest of the app — the counterpart to CableSetupComponent on Windows. */
class BlackHoleSetupComponent : public juce::Component
{
public:
    explicit BlackHoleSetupComponent (juce::AudioDeviceManager& deviceManager);

    /** Opens the walkthrough as an async dialog (single-instance, dark title bar). */
    static void launch (juce::AudioDeviceManager& deviceManager);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void recheck();
    void refreshContent();

    juce::AudioDeviceManager& deviceManager;

    juce::Label      heading;
    juce::Label      statusLabel;
    juce::TextEditor body;
    juce::TextButton copyBrewButton { "COPY HOMEBREW COMMAND" };
    juce::TextButton recheckButton  { "RE-CHECK" };
    juce::TextButton closeButton    { "CLOSE" };
    juce::HyperlinkButton pageLink { "Open the BlackHole download page",
                                     juce::URL (BlackHole::downloadPage) };
    juce::HyperlinkButton githubLink { "BlackHole is free & open-source (Existential Audio)",
                                       juce::URL (BlackHole::githubReleases) };

    juce::String detected;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlackHoleSetupComponent)
};

} // namespace play

#endif // JUCE_MAC

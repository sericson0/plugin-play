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
    this is a *guided* install: detect, point the user at the official download,
    and re-check.
*/
namespace VirtualCable
{
    /** Official VB-CABLE download page (user installs it themselves — licence-safe). */
    inline const juce::String downloadPage { "https://vb-audio.com/Cable/" };

    /** True if a device name looks like a known virtual audio cable. */
    bool nameLooksLikeCable (const juce::String& deviceName);

    /** Scans all audio device types for an installed virtual cable.
        Returns the matched device name, or an empty string if none is found. */
    juce::String findInstalled (juce::AudioDeviceManager& deviceManager);
}

//==============================================================================
/** Guided VB-CABLE install / configuration walkthrough, shown in a dialog. */
class CableSetupComponent : public juce::Component
{
public:
    explicit CableSetupComponent (juce::AudioDeviceManager& deviceManager);

    /** Opens the walkthrough as an async dialog styled like the rest of the app. */
    static void launch (juce::AudioDeviceManager& deviceManager);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void recheck();
    void refreshContent();

    juce::AudioDeviceManager& deviceManager;

    juce::Label       heading;
    juce::Label       statusLabel;
    juce::TextEditor  body;
    juce::TextButton  downloadButton { "DOWNLOAD VB-CABLE" };
    juce::TextButton  recheckButton  { "RE-CHECK" };
    juce::TextButton  closeButton    { "CLOSE" };

    juce::String detectedCable;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CableSetupComponent)
};

} // namespace play

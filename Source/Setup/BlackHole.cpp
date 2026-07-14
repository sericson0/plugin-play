#include "BlackHole.h"

#if JUCE_MAC

#include "../Theme.h"

namespace play
{

using namespace play::Colours;

//==============================================================================
namespace BlackHole
{
    bool nameLooksLikeVirtualDevice (const juce::String& deviceName)
    {
        const auto n = deviceName.toLowerCase();

        // BlackHole ("BlackHole 2ch/16ch/64ch"), plus the other common Mac virtual
        // audio drivers so we recognise a working setup the user already has.
        return n.contains ("blackhole")
            || n.contains ("existential")
            || n.contains ("loopback")          // Rogue Amoeba Loopback
            || n.contains ("soundflower")
            || n.contains ("virtual audio");
    }

    juce::String findInstalled (juce::AudioDeviceManager& deviceManager, bool rescan)
    {
        // getAvailableDeviceTypes() runs the one-time startup scan if it hasn't
        // happened yet, so the fast path never reads an unscanned list.
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            if (type == nullptr)
                continue;

            if (rescan)
                type->scanForDevices();

            for (bool wantInputs : { true, false })
                for (const auto& name : type->getDeviceNames (wantInputs))
                    if (nameLooksLikeVirtualDevice (name))
                        return name;
        }

        return {};
    }
}

//==============================================================================
BlackHoleSetupComponent::BlackHoleSetupComponent (juce::AudioDeviceManager& dm)
    : deviceManager (dm)
{
    heading.setText ("Audio Routing Setup (BlackHole)", juce::dontSendNotification);
    heading.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    heading.setColour (juce::Label::textColourId, textBright);
    addAndMakeVisible (heading);

    statusLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    body.setMultiLine (true);
    body.setReadOnly (true);
    body.setCaretVisible (false);
    body.setScrollbarsShown (true);
    body.setColour (juce::TextEditor::backgroundColourId, panelBackground);
    body.setColour (juce::TextEditor::outlineColourId, gridLine);
    body.setColour (juce::TextEditor::focusedOutlineColourId, gridLine);
    body.setColour (juce::TextEditor::textColourId, textNormal);
    body.setFont (juce::Font (juce::FontOptions (14.0f)));
    addAndMakeVisible (body);

    copyBrewButton.setTooltip ("Copy \"" + BlackHole::homebrewCommand + "\" to the clipboard");
    copyBrewButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (BlackHole::homebrewCommand);
        statusLabel.setColour (juce::Label::textColourId, accentBright);
        statusLabel.setText ("Copied \"" + BlackHole::homebrewCommand + "\" - paste it into Terminal.",
                             juce::dontSendNotification);
    };
    recheckButton.onClick = [this] { recheck(); };
    closeButton.onClick   = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };

    addAndMakeVisible (copyBrewButton);
    addAndMakeVisible (recheckButton);
    addAndMakeVisible (closeButton);

    for (auto* link : { &pageLink, &githubLink })
    {
        link->setJustificationType (juce::Justification::centredLeft);
        link->setColour (juce::HyperlinkButton::textColourId, accent);
        addAndMakeVisible (*link);
    }
    githubLink.setColour (juce::HyperlinkButton::textColourId, textNormal);

    detected = BlackHole::findInstalled (deviceManager, false);
    refreshContent();

    setSize (520, 470);
}

//==============================================================================
void BlackHoleSetupComponent::launch (juce::AudioDeviceManager& deviceManager)
{
    // Only one setup dialog at a time: a second open request raises the existing
    // window instead of stacking a duplicate.
    static juce::Component::SafePointer<juce::DialogWindow> openDialog;

    if (openDialog != nullptr)
    {
        openDialog->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new BlackHoleSetupComponent (deviceManager));
    options.dialogTitle = "Audio Routing Setup";
    options.dialogBackgroundColour = background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    if (auto* window = options.launchAsync())
    {
        openDialog = window;
        applyDarkTitleBar (*window);
    }
}

//==============================================================================
void BlackHoleSetupComponent::recheck()
{
    detected = BlackHole::findInstalled (deviceManager, true);   // forces a fresh scan
    refreshContent();
}

void BlackHoleSetupComponent::refreshContent()
{
    if (detected.isNotEmpty())
    {
        statusLabel.setColour (juce::Label::textColourId, metricGood);
        statusLabel.setText ("Found: " + detected, juce::dontSendNotification);

        copyBrewButton.setButtonText ("COPY HOMEBREW COMMAND");   // still handy for the 16/64ch variants

        body.setText (
            "BlackHole is installed - you're ready to route audio through Plugin Play.\n"
            "\n"
            "To send an app (your DJ software, Spotify, a browser...) through Plugin Play:\n"
            "\n"
            "1.  In that app's own sound/output settings, choose \"" + detected + "\"\n"
            "    as its output device. (No per-app setting? Set the macOS system output\n"
            "    to BlackHole in System Settings > Sound instead.)\n"
            "\n"
            "2.  Back here in Plugin Play, pick \"" + detected + "\" in the INPUT dropdown.\n"
            "\n"
            "3.  Pick your speakers or audio interface in the OUTPUT dropdown.\n"
            "\n"
            "Now the app's sound flows into Plugin Play, through your effects, and out to\n"
            "your speakers - you'll hear the processed signal. Nothing plays out of the app\n"
            "directly while it's routed to BlackHole.\n"
            "\n"
            "Tip: on macOS 14.4 or newer, Plugin Play can capture an app directly with no\n"
            "routing at all - just pick it under \"Capture an app\" in the INPUT dropdown.",
            juce::dontSendNotification);
    }
    else
    {
        statusLabel.setColour (juce::Label::textColourId, metricWarn);
        statusLabel.setText ("No virtual audio device found", juce::dontSendNotification);

        copyBrewButton.setButtonText ("COPY HOMEBREW COMMAND");

        body.setText (
            "Plugin Play needs a virtual audio device to route another app's sound into it.\n"
            "The free, open-source BlackHole driver is the recommended choice.\n"
            "\n"
            "Easiest (if you have Homebrew):\n"
            "  1.  Click \"Copy Homebrew command\" below.\n"
            "  2.  Open Terminal, paste, and press Return:\n"
            "        " + BlackHole::homebrewCommand + "\n"
            "  3.  Enter your password if asked, then come back and click \"Re-check\".\n"
            "\n"
            "Prefer a regular installer?\n"
            "  Use \"Open the BlackHole download page\" below, download the installer,\n"
            "  run it, then come back and click \"Re-check\".\n"
            "\n"
            "Once BlackHole is installed, this window will show you how to route an app\n"
            "through Plugin Play.",
            juce::dontSendNotification);
    }
}

//==============================================================================
void BlackHoleSetupComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void BlackHoleSetupComponent::resized()
{
    auto area = getLocalBounds().reduced (18, 16);

    heading.setBounds (area.removeFromTop (30));
    area.removeFromTop (2);
    statusLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);

    auto buttonRow = area.removeFromBottom (34);
    closeButton.setBounds (buttonRow.removeFromRight (100));
    buttonRow.removeFromRight (8);
    recheckButton.setBounds (buttonRow.removeFromRight (110));
    copyBrewButton.setBounds (buttonRow.removeFromLeft (230));

    area.removeFromBottom (10);
    githubLink.setBounds (area.removeFromBottom (20));
    pageLink.setBounds (area.removeFromBottom (20));
    area.removeFromBottom (8);

    body.setBounds (area);
}

} // namespace play

#endif // JUCE_MAC

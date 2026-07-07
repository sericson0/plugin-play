#include "VirtualCable.h"
#include "../Theme.h"

namespace play
{

using namespace play::Colours;

//==============================================================================
namespace VirtualCable
{
    bool nameLooksLikeCable (const juce::String& deviceName)
    {
        const auto n = deviceName.toLowerCase();

        // VB-CABLE ("CABLE Input/Output (VB-Audio Virtual Cable)"), plus the other
        // common virtual cables so we recognise a working setup the user already has.
        return n.contains ("vb-audio")
            || n.contains ("cable input")
            || n.contains ("cable output")
            || n.contains ("voicemeeter")
            || n.contains ("virtual audio cable")
            || n.contains ("vac ");
    }

    juce::String findInstalled (juce::AudioDeviceManager& deviceManager)
    {
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            if (type == nullptr)
                continue;

            type->scanForDevices();

            for (bool wantInputs : { true, false })
                for (const auto& name : type->getDeviceNames (wantInputs))
                    if (nameLooksLikeCable (name))
                        return name;
        }

        return {};
    }
} // namespace VirtualCable

//==============================================================================
CableSetupComponent::CableSetupComponent (juce::AudioDeviceManager& dm)
    : deviceManager (dm)
{
    heading.setText ("Virtual Cable Setup", juce::dontSendNotification);
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

    downloadButton.onClick = [] { juce::URL (VirtualCable::downloadPage).launchInDefaultBrowser(); };
    recheckButton.onClick  = [this] { recheck(); };
    closeButton.onClick    = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };

    addAndMakeVisible (downloadButton);
    addAndMakeVisible (recheckButton);
    addAndMakeVisible (closeButton);

    detectedCable = VirtualCable::findInstalled (deviceManager);
    refreshContent();

    setSize (520, 420);
}

//==============================================================================
void CableSetupComponent::launch (juce::AudioDeviceManager& deviceManager)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new CableSetupComponent (deviceManager));
    options.dialogTitle = "Virtual Cable Setup";
    options.dialogBackgroundColour = background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    if (auto* window = options.launchAsync())
        applyDarkTitleBar (*window);
}

//==============================================================================
void CableSetupComponent::recheck()
{
    detectedCable = VirtualCable::findInstalled (deviceManager);
    refreshContent();
}

void CableSetupComponent::refreshContent()
{
    const bool installed = detectedCable.isNotEmpty();

    if (installed)
    {
        statusLabel.setColour (juce::Label::textColourId, metricGood);
        statusLabel.setText ("Detected:  " + detectedCable, juce::dontSendNotification);

        body.setText (
            "A virtual cable is installed, so you can route your DJ software through "
            "Plugin Play as a device.\n"
            "\n"
            "1.  In your DJ software, set the OUTPUT device to\n"
            "    \"CABLE Input (VB-Audio Virtual Cable)\".\n"
            "\n"
            "2.  In Plugin Play -> Audio Settings, set the INPUT device to\n"
            "    \"CABLE Output (VB-Audio Virtual Cable)\".\n"
            "\n"
            "3.  Set Plugin Play's OUTPUT to your interface (ASIO recommended).\n"
            "\n"
            "Signal flow:\n"
            "    DJ software  ->  cable  ->  Plugin Play (FX)  ->  your DAC\n"
            "\n"
            "Keep every stage at the same sample rate (usually 44.1 kHz) and leave "
            "\"Audio Enhancements\" off on the cable to stay bit-transparent.\n"
            "\n"
            "Prefer no install? You can skip the cable entirely and just pick your DJ "
            "app as the input source inside Plugin Play.",
            juce::dontSendNotification);

        downloadButton.setButtonText ("RE-DOWNLOAD VB-CABLE");
    }
    else
    {
        statusLabel.setColour (juce::Label::textColourId, metricWarn);
        statusLabel.setText ("No virtual cable found", juce::dontSendNotification);

        body.setText (
            "This step is OPTIONAL. It lets Plugin Play appear as an output device "
            "inside your DJ software. (You can also just pick your DJ app directly as "
            "the input source in Plugin Play, with nothing to install.)\n"
            "\n"
            "To install the recommended free cable, VB-CABLE:\n"
            "\n"
            "1.  Click \"Download VB-CABLE\" below.\n"
            "2.  Unzip the downloaded folder.\n"
            "3.  Right-click \"VBCABLE_Setup_x64.exe\" -> Run as administrator.\n"
            "4.  Reboot Windows.\n"
            "5.  Come back here and click \"Re-check\".\n"
            "\n"
            "Once it's installed, this window will show how to route your DJ software "
            "through Plugin Play.",
            juce::dontSendNotification);

        downloadButton.setButtonText ("DOWNLOAD VB-CABLE");
    }
}

//==============================================================================
void CableSetupComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void CableSetupComponent::resized()
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
    buttonRow.removeFromLeft (0);
    downloadButton.setBounds (buttonRow.removeFromLeft (190));

    area.removeFromBottom (10);
    body.setBounds (area);
}

} // namespace play

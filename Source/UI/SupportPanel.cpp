#include "SupportPanel.h"
#include "../Theme.h"

namespace play
{

using namespace play::Colours;

SupportPanel::SupportPanel()
{
    heading.setText ("Support Plugin Play", juce::dontSendNotification);
    heading.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    heading.setColour (juce::Label::textColourId, accentBright);
    addAndMakeVisible (heading);

    intro.setText ("Plugin Play is free and open source. If it helps your sets, a tip keeps "
                   "development going and the tools free for everyone. Thank you!",
                   juce::dontSendNotification);
    intro.setFont (juce::Font (juce::FontOptions (13.5f)));
    intro.setColour (juce::Label::textColourId, textNormal);
    intro.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (intro);

    donateLabel.setText ("DONATE", juce::dontSendNotification);
    donateLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    donateLabel.setColour (juce::Label::textColourId, gridText);
    addAndMakeVisible (donateLabel);

    // Card / Venmo open in the browser; keep them prominent and underlined.
    for (auto* link : { &stripeButton, &venmoButton, &emailButton })
    {
        link->setColour (juce::HyperlinkButton::textColourId, accentBright);
        link->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*link);
    }
    stripeButton.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)), false, juce::Justification::centredLeft);
    venmoButton .setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)), false, juce::Justification::centredLeft);
    emailButton .setFont (juce::Font (juce::FontOptions (13.0f)),                  false, juce::Justification::centredLeft);

    stripeButton.setTooltip (Donate::stripeUrl);
    venmoButton .setTooltip (Donate::venmoUrl);

    // Zelle is a phone number, not a link — show it with a copy button.
    zelleLabel.setText ("Zelle", juce::dontSendNotification);
    zelleLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    zelleLabel.setColour (juce::Label::textColourId, textBright);
    addAndMakeVisible (zelleLabel);

    zelleValue.setText (Donate::zelleNumber, juce::dontSendNotification);
    zelleValue.setFont (juce::Font (juce::FontOptions (15.0f)));
    zelleValue.setColour (juce::Label::textColourId, textNormal);
    addAndMakeVisible (zelleValue);

    copyZelleButton.setColour (juce::TextButton::textColourOffId, accentBright);
    copyZelleButton.setTooltip ("Copy the Zelle number to the clipboard");
    copyZelleButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (Donate::zelleNumber);
        copyZelleButton.setButtonText ("Copied");

        juce::Component::SafePointer<SupportPanel> safe (this);
        juce::Timer::callAfterDelay (1500, [safe]
        {
            if (safe != nullptr)
                safe->copyZelleButton.setButtonText ("Copy");
        });
    };
    addAndMakeVisible (copyZelleButton);

    contactLabel.setText ("Ideas, resources or bug reports are welcome too:", juce::dontSendNotification);
    contactLabel.setFont (juce::Font (juce::FontOptions (12.5f)));
    contactLabel.setColour (juce::Label::textColourId, textNormal);
    contactLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (contactLabel);

    setSize (460, 366);
}

void SupportPanel::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // A thin separator under the intro, and above the contact row.
    g.setColour (gridLine);
    g.fillRect (20, 118, getWidth() - 40, 1);
    g.fillRect (20, getHeight() - 66, getWidth() - 40, 1);
}

void SupportPanel::resized()
{
    auto area = getLocalBounds().reduced (20, 18);

    heading.setBounds (area.removeFromTop (28));
    area.removeFromTop (6);
    intro.setBounds (area.removeFromTop (52));
    area.removeFromTop (14);   // clears the separator drawn in paint()

    donateLabel.setBounds (area.removeFromTop (16));
    area.removeFromTop (4);

    stripeButton.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);
    venmoButton.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);

    auto zelleRow = area.removeFromTop (26);
    zelleLabel.setBounds (zelleRow.removeFromLeft (56));
    copyZelleButton.setBounds (zelleRow.removeFromRight (70));
    zelleRow.removeFromRight (8);
    zelleValue.setBounds (zelleRow);

    // Contact row sits below the lower separator.
    auto contactRow = getLocalBounds().reduced (20, 18);
    contactRow = contactRow.removeFromBottom (40);
    contactLabel.setBounds (contactRow.removeFromTop (18));
    emailButton.setBounds (contactRow.removeFromTop (20));
}

void SupportPanel::launch()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new SupportPanel());
    options.dialogTitle = "Support Plugin Play";
    options.dialogBackgroundColour = Colours::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    if (auto* window = options.launchAsync())
        applyDarkTitleBar (*window);
}

} // namespace play

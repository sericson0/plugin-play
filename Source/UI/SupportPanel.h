#pragma once

#include <JuceHeader.h>

namespace play
{

//==============================================================================
/** Donation destinations, shared with the wider toolkit (Stripe / Venmo / Zelle).
    Same endpoints used by the Tango Toolkit "Support" flow. */
namespace Donate
{
    inline const juce::String stripeUrl    { "https://buy.stripe.com/5kQ8wOfGU7bsdZ9gplgnK01" };
    inline const juce::String venmoUrl     { "https://venmo.com/Sean-Ericson-1" };
    inline const juce::String zelleNumber  { "925-528-9221" };
    inline const juce::String contactEmail { "TangoToolkit@gmail.com" };
}

//==============================================================================
/** "Support Plugin Play" panel — card/Apple Pay (Stripe), Venmo, and Zelle, plus
    a contact link. Shown as a themed dialog; also embedded in the welcome guide. */
class SupportPanel : public juce::Component
{
public:
    SupportPanel();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens the panel as an async dialog styled like the rest of the app. */
    static void launch();

private:
    juce::Label heading, intro, donateLabel, zelleLabel, zelleValue, contactLabel;
    juce::HyperlinkButton stripeButton { "Card / Apple Pay", juce::URL (Donate::stripeUrl) };
    juce::HyperlinkButton venmoButton  { "Venmo",           juce::URL (Donate::venmoUrl) };
    juce::HyperlinkButton emailButton  { "Email " + Donate::contactEmail,
                                         juce::URL ("mailto:" + Donate::contactEmail) };
    juce::TextButton copyZelleButton { "Copy" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SupportPanel)
};

} // namespace play

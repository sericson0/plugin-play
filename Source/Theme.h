#pragma once

#include <JuceHeader.h>

//==============================================================================
//  Palette — ported from the hisstory project so both apps share a look:
//  dark navy background with deep-orange / golden accents.
//==============================================================================
namespace play::Colours
{
    const juce::Colour background      { 0xff12151f };
    const juce::Colour panelBackground { 0xff0b0e17 };
    const juce::Colour gridLine        { 0xff1e2230 };
    const juce::Colour gridText        { 0xff5a5e70 };
    const juce::Colour textNormal      { 0xffb0b4c0 };
    const juce::Colour textBright      { 0xfff0f0f0 };
    const juce::Colour accent          { 0xffD96C30 };   // deep orange
    const juce::Colour accentBright    { 0xffF3A10F };   // golden orange
    const juce::Colour accentDim       { 0xff8B4420 };
    const juce::Colour buttonSelected  { 0xffA34210 };
    const juce::Colour inactive        { 0xff5a5e70 };
    const juce::Colour sliderTrack     { 0xff2a2e3e };
    const juce::Colour buttonBg        { 0xff2a2e42 };
    const juce::Colour buttonBgHover   { 0xff353a50 };
    const juce::Colour metricGood      { 0xff4CAF50 };
    const juce::Colour metricWarn      { 0xffFF9800 };
    const juce::Colour metricBad       { 0xffF44336 };
}

namespace play
{

/** Asks DWM to draw the native title bar dark so it matches the theme.
    Call after the window is visible (its peer must exist). No-op off Windows. */
void applyDarkTitleBar (juce::Component& window);

class PlayLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PlayLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

} // namespace play

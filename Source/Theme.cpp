#include "Theme.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <dwmapi.h>
 #pragma comment (lib, "dwmapi.lib")
#endif

namespace play
{

using namespace play::Colours;

void applyDarkTitleBar (juce::Component& window)
{
#if JUCE_WINDOWS
    if (auto* peer = window.getPeer())
    {
        const BOOL useDarkMode = TRUE;
        DwmSetWindowAttribute ((HWND) peer->getNativeHandle(),
                               20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                               &useDarkMode, sizeof (useDarkMode));
    }
#else
    juce::ignoreUnused (window);
#endif
}

PlayLookAndFeel::PlayLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::textColourId,        textBright);
    setColour (juce::Label::textColourId,                 textNormal);

    setColour (juce::TextButton::buttonColourId,   buttonBg);
    setColour (juce::TextButton::buttonOnColourId, buttonSelected);
    setColour (juce::TextButton::textColourOffId,  textNormal);
    setColour (juce::TextButton::textColourOnId,   textBright);

    setColour (juce::ComboBox::backgroundColourId,     buttonBg);
    setColour (juce::ComboBox::textColourId,           textNormal);
    setColour (juce::ComboBox::outlineColourId,        gridLine.brighter (0.3f));
    setColour (juce::ComboBox::buttonColourId,         buttonBg);
    setColour (juce::ComboBox::arrowColourId,          textNormal);
    setColour (juce::ComboBox::focusedOutlineColourId, accent);

    setColour (juce::PopupMenu::backgroundColourId,             background.brighter (0.05f));
    setColour (juce::PopupMenu::textColourId,                   textNormal);
    setColour (juce::PopupMenu::headerTextColourId,             accentBright);
    setColour (juce::PopupMenu::highlightedBackgroundColourId,  buttonSelected);
    setColour (juce::PopupMenu::highlightedTextColourId,        textBright);

    setColour (juce::ListBox::backgroundColourId,   panelBackground);
    setColour (juce::ScrollBar::thumbColourId,      buttonBgHover);
    setColour (juce::TooltipWindow::backgroundColourId, background.brighter (0.1f));
    setColour (juce::TooltipWindow::textColourId,       textNormal);

    setColour (juce::AlertWindow::backgroundColourId, background.brighter (0.05f));
    setColour (juce::AlertWindow::textColourId,       textNormal);
    setColour (juce::AlertWindow::outlineColourId,    gridLine.brighter (0.3f));

    setColour (juce::ToggleButton::textColourId,     textNormal);
    setColour (juce::ToggleButton::tickColourId,     accentBright);
    setColour (juce::ToggleButton::tickDisabledColourId, inactive);

    setColour (juce::Slider::backgroundColourId,        sliderTrack);
    setColour (juce::Slider::trackColourId,             sliderTrack);
    setColour (juce::Slider::thumbColourId,             accent);
    setColour (juce::Slider::rotarySliderFillColourId,  accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, sliderTrack);
    setColour (juce::Slider::textBoxTextColourId,       textBright);
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);

    setColour (juce::TextEditor::backgroundColourId, panelBackground);
    setColour (juce::TextEditor::textColourId,       textBright);
    setColour (juce::TextEditor::outlineColourId,    gridLine.brighter (0.3f));
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::CaretComponent::caretColourId,  accentBright);
}

void PlayLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                            const juce::Colour& backgroundColour,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

    juce::Colour bg = backgroundColour;
    if (shouldDrawButtonAsDown)
        bg = buttonSelected;
    else if (shouldDrawButtonAsHighlighted)
        bg = buttonBgHover;

    g.setColour (bg);
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (button.getToggleState() ? buttonSelected.brighter (0.3f)
                                         : gridLine.brighter (0.3f));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
}

juce::Font PlayLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions ((float) juce::jmin (14, buttonHeight - 6), juce::Font::bold));
}

} // namespace play

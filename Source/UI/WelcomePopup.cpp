#include "WelcomePopup.h"
#include "SupportPanel.h"
#include "../Theme.h"

namespace play
{

using namespace play::Colours;

namespace
{
    constexpr int popupWidth   = 560;
    constexpr int popupHeight  = 340;
    constexpr int bulletGutter = 22;
}

WelcomePopup::WelcomePopup (juce::PropertiesFile& config)
    : config_ (config)
{
    setSize (popupWidth, popupHeight);

    loadStepContent();

    titleLabel_.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    titleLabel_.setColour (juce::Label::textColourId, accentBright);
    titleLabel_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel_);

    stepIndicatorLabel_.setFont (juce::Font (juce::FontOptions (12.0f)));
    stepIndicatorLabel_.setColour (juce::Label::textColourId, gridText);
    stepIndicatorLabel_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (stepIndicatorLabel_);

    backButton_.onClick    = [this] { if (currentStep_ > 0)              goToStep (currentStep_ - 1); };
    nextButton_.onClick    = [this] { if (currentStep_ < totalSteps - 1) goToStep (currentStep_ + 1); };
    doneButton_.onClick    = [this] { finishAndClose(); };
    supportButton_.onClick = [] { SupportPanel::launch(); };

    doneButton_.setColour   (juce::TextButton::textColourOffId, accentBright);
    supportButton_.setColour (juce::TextButton::textColourOffId, accentBright);
    supportButton_.setTooltip ("Support Plugin Play — Card / Apple Pay, Venmo or Zelle");

    addAndMakeVisible (backButton_);
    addAndMakeVisible (nextButton_);
    addAndMakeVisible (doneButton_);
    addAndMakeVisible (supportButton_);

    dontShowAgainToggle_.setToggleState (true, juce::dontSendNotification);
    dontShowAgainToggle_.setColour (juce::ToggleButton::textColourId, textNormal);
    dontShowAgainToggle_.setColour (juce::ToggleButton::tickColourId, accentBright);
    addAndMakeVisible (dontShowAgainToggle_);

    goToStep (0);
}

WelcomePopup::~WelcomePopup()
{
    if (completed_)
    {
        // Clear any installer-created marker so later launches don't re-show.
        markerFile().deleteFile();

        if (dontShowAgainToggle_.getToggleState())
        {
            config_.setValue ("seenWelcome", true);
            config_.saveIfNeeded();
        }
    }
}

juce::File WelcomePopup::markerFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("PluginPlay")
             .getChildFile (".show-welcome");
}

void WelcomePopup::loadStepContent()
{
    steps_.clearQuick();

    {
        StepContent s;
        s.title = "Welcome to Plugin Play";
        s.lines.add ({ "Plugin Play adds VST3 effects to your music in real time.", false });
        s.lines.add ({ "Your DJ software or any app plays in, passes through your", false });
        s.lines.add ({ "chain of effects, and comes out your speakers.", false });
        s.lines.add ({ "Nothing is changed on disk - it is all live.", false });
        steps_.add (s);
    }
    {
        StepContent s;
        s.title = "Set up a virtual cable";
        s.lines.add ({ "Plugin Play needs your audio as an input. A virtual cable is a", false });
        s.lines.add ({ "software 'wire' that carries sound between apps.", false });
        s.lines.add ({ "Click VIRTUAL CABLE in the header to install VB-CABLE - it", false });
        s.lines.add ({ "walks you through a one-click download, then a reboot.", false });
        steps_.add (s);
    }
    {
        StepContent s;
        s.title = "Choose your input";
        s.lines.add ({ "Easiest: open the INPUT dropdown and pick an app under", false });
        s.lines.add ({ "'Send an app through Plugin Play' - it routes that app for you.", false });
        s.lines.add ({ "Or route manually: set your DJ software's output to the cable,", false });
        s.lines.add ({ "then set INPUT to the cable and OUTPUT to your speakers.", false });
        steps_.add (s);
    }
    {
        StepContent s;
        s.title = "Add your effects";
        s.lines.add ({ "Click SCAN PLUGINS once to find your installed VST3 effects.", false });
        s.lines.add ({ "Use + Add Plugin to build the chain; audio flows top to bottom.", false });
        s.lines.add ({ "Drag cards to reorder, ON/OFF to bypass, OPEN for a plugin's GUI.", false });
        s.lines.add ({ "X removes an effect - Ctrl+Z brings it back.", false });
        steps_.add (s);
    }
    {
        StepContent s;
        s.title = "Master controls & presets";
        s.lines.add ({ "FX ON/OFF above the meters bypasses every effect at once.", false });
        s.lines.add ({ "LIMITER is a safety brickwall on the output - leave it on to", false });
        s.lines.add ({ "protect your speakers from a runaway plugin level.", false });
        s.lines.add ({ "Click a meter to reset its clip light. Save chains from PRESETS.", false });
        steps_.add (s);
    }
    {
        StepContent s;
        s.title = "You're all set";
        s.lines.add ({ "Hover any control for a tooltip, and open HELP any time for", false });
        s.lines.add ({ "full documentation and troubleshooting.", false });
        s.lines.add ({ "Plugin Play is free and open source. If it helps your sets,", false });
        s.lines.add ({ "please consider a tip - hit SUPPORT below. Thank you!", false });
        steps_.add (s);
    }
}

void WelcomePopup::goToStep (int newStep)
{
    currentStep_ = juce::jlimit (0, totalSteps - 1, newStep);

    const auto& step = steps_.getReference (currentStep_);
    titleLabel_.setText (step.title, juce::dontSendNotification);
    stepIndicatorLabel_.setText (juce::String (currentStep_ + 1) + " / " + juce::String (totalSteps),
                                 juce::dontSendNotification);

    bodyLines_.clear();
    for (const auto& line : step.lines)
    {
        auto* label = new juce::Label();
        label->setText (line.text, juce::dontSendNotification);
        label->setFont (juce::Font (juce::FontOptions (14.0f)));
        label->setColour (juce::Label::textColourId, line.warn ? metricWarn : textNormal);
        label->setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (label);
        bodyLines_.add (label);
    }

    updateButtonVisibility();
    resized();
    repaint();
}

void WelcomePopup::updateButtonVisibility()
{
    backButton_.setEnabled (currentStep_ > 0);

    const bool onLast = (currentStep_ == totalSteps - 1);
    nextButton_.setVisible (! onLast);
    doneButton_.setVisible (onLast);
    supportButton_.setVisible (onLast);
    dontShowAgainToggle_.setVisible (onLast);
}

void WelcomePopup::finishAndClose()
{
    completed_ = true;
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (0);
}

void WelcomePopup::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Bullet points beside each non-warning body line.
    g.setColour (accent);
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    for (int i = 0; i < bodyLines_.size(); ++i)
    {
        const auto& line = steps_.getReference (currentStep_).lines.getReference (i);
        if (line.warn)
            continue;

        auto b = bodyLines_.getUnchecked (i)->getBounds();
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2")),
                    juce::Rectangle<int> (b.getX() - bulletGutter, b.getY(), bulletGutter, b.getHeight()),
                    juce::Justification::centredLeft);
    }

    // Separator above the footer.
    g.setColour (gridLine);
    g.fillRect (16, getHeight() - 48, getWidth() - 32, 1);
}

void WelcomePopup::resized()
{
    auto area = getLocalBounds().reduced (18);

    titleLabel_.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);

    auto footer = area.removeFromBottom (34);
    const int btnW = 88;
    const int gap  = 8;

    // Left: step indicator, then the "Don't show again" toggle on the last step.
    stepIndicatorLabel_.setBounds (footer.removeFromLeft (54));
    if (dontShowAgainToggle_.isVisible())
    {
        footer.removeFromLeft (gap);
        dontShowAgainToggle_.setBounds (footer.removeFromLeft (170));
    }

    // Right: Back, then Next / Done (shared rightmost slot), with SUPPORT before them
    // on the last step.
    auto rightSlot = footer.removeFromRight (btnW);
    doneButton_.setBounds (rightSlot);
    nextButton_.setBounds (rightSlot);
    footer.removeFromRight (gap);
    backButton_.setBounds (footer.removeFromRight (btnW));
    if (supportButton_.isVisible())
    {
        footer.removeFromRight (gap);
        supportButton_.setBounds (footer.removeFromRight (btnW));
    }

    area.removeFromBottom (14);   // clears the separator

    stepIndicatorLabel_.toFront (false);

    const int lineH = 22;
    auto bodyArea = area.withTrimmedLeft (bulletGutter);
    for (auto* label : bodyLines_)
    {
        label->setBounds (bodyArea.removeFromTop (lineH));
        bodyArea.removeFromTop (2);
    }
}

void WelcomePopup::show (juce::PropertiesFile& config)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new WelcomePopup (config));
    options.dialogTitle = "Welcome to Plugin Play";
    options.dialogBackgroundColour = Colours::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    if (auto* window = options.launchAsync())
        applyDarkTitleBar (*window);
}

} // namespace play

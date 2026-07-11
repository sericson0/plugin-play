#include "WelcomePopup.h"
#include "SupportPanel.h"
#include "../Setup/VirtualCable.h"
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

WelcomePopup::WelcomePopup (juce::PropertiesFile& config, juce::AudioDeviceManager& deviceManager)
    : config_ (config), deviceManager_ (deviceManager)
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
    cableButton_.onClick   = [this] { CableSetupComponent::launch (deviceManager_); };

    doneButton_.setColour   (juce::TextButton::textColourOffId, accentBright);
    supportButton_.setColour (juce::TextButton::textColourOffId, accentBright);
    supportButton_.setTooltip ("Support Plugin Play — Card / Apple Pay, Venmo or Zelle");
    cableButton_.setColour (juce::TextButton::textColourOffId, accentBright);
    cableButton_.setTooltip ("Guided VB-CABLE install — one download, approve the prompts, reboot");

    addAndMakeVisible (backButton_);
    addAndMakeVisible (nextButton_);
    addAndMakeVisible (doneButton_);
    addAndMakeVisible (supportButton_);
    addChildComponent (cableButton_);   // shown only on the cable step when none is installed

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
        s.title = "Welcome!";
        s.lines.add ({ "Plugin Play runs your music through VST3 effects, live." });
        s.lines.add ({ "Any app plays in - your DJ software, Spotify, anything." });
        s.lines.add ({ "The processed sound comes out your speakers." });
        s.lines.add ({ "Your tracks on disk are never changed." });
        steps_.add (s);
    }
    {
        // Body lines are filled in by refreshCableStep() when the step is shown,
        // after scanning the audio devices for an installed cable.
        StepContent s;
        s.title = "Set up a virtual cable";
        steps_.add (s);
    }
    {
        StepContent s;
        s.title = "Choose your input";
        s.lines.add ({ "Easiest: open the INPUT dropdown and pick a running app." });
        s.lines.add ({ "Plugin Play routes that app through the cable for you." });
        s.lines.add ({ "Prefer manual routing? Set your DJ software's output to the" });
        s.lines.add ({ "cable, then INPUT to the cable and OUTPUT to your speakers.", false, true });
        steps_.add (s);
    }
    {
        StepContent s;
        s.title = "Add your effects";
        s.lines.add ({ "+ Add Plugin builds the chain - audio flows top to bottom." });
        s.lines.add ({ "Drag cards by their grip dots to reorder. Each card's buttons:" });
        s.lines.add ({ "ON / OFF - turn that one effect on or off (bypass)." });
        s.lines.add ({ "OPEN - the plugin's own window (or double-click the card)." });
        s.lines.add ({ "FLOAT - keep that window on top of your DJ software." });
        s.lines.add ({ "X - remove the effect; Ctrl+Z brings it back." });
        s.lines.add ({ "Run SCAN PLUGINS after installing new plugins." });
        steps_.add (s);
    }
    {
        StepContent s;
        s.title = "Master controls & presets";
        s.lines.add ({ "FX ON/OFF above the meters bypasses every effect at once." });
        s.lines.add ({ "LIMITER guards your speakers from runaway levels - leave it on." });
        s.lines.add ({ "Click a meter to reset its clip light." });
        s.lines.add ({ "Save chains you like from the PRESETS menu." });
        steps_.add (s);
    }
    {
        StepContent s;
        s.title = "You're all set";
        s.lines.add ({ "Hover any control for a tooltip; HELP covers everything else." });
        s.lines.add ({ "Replay this guide any time - GUIDE, top right inside HELP." });
        s.lines.add ({ "Plugin Play is free and open source, supported by tips." });
        s.lines.add ({ "If it helps your sets, hit SUPPORT below. Thank you!" });
        steps_.add (s);
    }
}

void WelcomePopup::refreshCableStep()
{
    detectedCable_ = VirtualCable::findInstalled (deviceManager_, false);

    auto& lines = steps_.getReference (cableStepIndex).lines;
    lines.clearQuick();

    lines.add ({ "A virtual cable is a software wire that carries sound" });
    lines.add ({ "between apps - it's how your music reaches Plugin Play.", false, true });

    if (detectedCable_.isNotEmpty())
    {
        lines.add ({ juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 Installed:  ")) + detectedCable_,
                     false, false, true });
        lines.add ({ "You're all set - nothing to install here." });
    }
    else
    {
        lines.add ({ "No virtual cable was found - Plugin Play needs one to work.", true });
        lines.add ({ "Click INSTALL CABLE below for a guided install -" });
        lines.add ({ "one quick download and a reboot, that's it.", false, true });
    }
}

void WelcomePopup::goToStep (int newStep)
{
    currentStep_ = juce::jlimit (0, totalSteps - 1, newStep);

    // Re-scan on every visit so the note stays honest if a cable appeared since
    // (e.g. the user came back to this step after running the guided install).
    if (currentStep_ == cableStepIndex)
        refreshCableStep();

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
        label->setColour (juce::Label::textColourId,
                          line.warn ? metricWarn : line.good ? metricGood : textNormal);
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
    cableButton_.setVisible (currentStep_ == cableStepIndex && detectedCable_.isEmpty());
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

    // Bullet points beside each body line, except status lines and continuation
    // lines (a wrapped sentence must not look like two separate bullets).
    g.setColour (accent);
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    for (int i = 0; i < bodyLines_.size(); ++i)
    {
        const auto& line = steps_.getReference (currentStep_).lines.getReference (i);
        if (line.warn || line.cont || line.good)
            continue;

        auto b = bodyLines_.getUnchecked (i)->getBounds();
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2")),
                    juce::Rectangle<int> (b.getX() - bulletGutter, b.getY(), bulletGutter, b.getHeight()),
                    juce::Justification::centredLeft);
    }

    // Separator above the footer, which starts at getHeight() - 18 (inset) - 34.
    g.setColour (gridLine);
    g.fillRect (16, getHeight() - 58, getWidth() - 32, 1);
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

    if (cableButton_.isVisible())
    {
        bodyArea.removeFromTop (10);
        cableButton_.setBounds (bodyArea.removeFromTop (30).removeFromLeft (150));
    }
}

void WelcomePopup::show (juce::PropertiesFile& config, juce::AudioDeviceManager& deviceManager)
{
    // Only one walkthrough at a time: a second GUIDE click raises the existing
    // dialog instead of stacking a duplicate.
    static juce::Component::SafePointer<juce::DialogWindow> openDialog;

    if (openDialog != nullptr)
    {
        openDialog->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new WelcomePopup (config, deviceManager));
    options.dialogTitle = "Welcome to Plugin Play";
    options.dialogBackgroundColour = Colours::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    if (auto* window = options.launchAsync())
    {
        openDialog = window;
        applyDarkTitleBar (*window);
    }
}

} // namespace play

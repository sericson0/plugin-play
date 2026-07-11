#pragma once

#include <JuceHeader.h>

namespace play
{

//==============================================================================
/** First-run walkthrough — a stepped guide shown the first time Plugin Play opens
    (and again after a reinstall, if the installer drops a .show-welcome marker).
    Modelled on the TigerTag welcome popup: title + bulleted steps, Back/Next/Done,
    a step indicator and a "Don't show this again" toggle on the final step. */
class WelcomePopup : public juce::Component
{
public:
    static constexpr int totalSteps = 6;

    WelcomePopup (juce::PropertiesFile& config, juce::AudioDeviceManager& deviceManager);
    ~WelcomePopup() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens the guide as a themed dialog. */
    static void show (juce::PropertiesFile& config, juce::AudioDeviceManager& deviceManager);

    /** Location of the installer's "re-show the welcome" marker. */
    static juce::File markerFile();

private:
    struct Line
    {
        juce::String text;
        bool warn = false;
        bool cont = false;   // continuation of the previous line: no bullet
        bool good = false;   // detection status: success colour, no bullet
    };

    struct StepContent
    {
        juce::String title;
        juce::Array<Line> lines;
    };

    static constexpr int cableStepIndex = 1;

    void loadStepContent();
    void refreshCableStep();
    void goToStep (int newStep);
    void updateButtonVisibility();
    void finishAndClose();

    juce::PropertiesFile& config_;
    juce::AudioDeviceManager& deviceManager_;
    juce::Array<StepContent> steps_;
    int currentStep_ = 0;
    bool completed_ = false;
    juce::String detectedCable_;

    juce::Label titleLabel_;
    juce::Label stepIndicatorLabel_;
    juce::OwnedArray<juce::Label> bodyLines_;
    juce::TextButton backButton_    { "BACK" };
    juce::TextButton nextButton_    { "NEXT" };
    juce::TextButton doneButton_    { "DONE" };
    juce::TextButton supportButton_ { "SUPPORT" };
    juce::TextButton cableButton_   { "INSTALL CABLE" };
    juce::ToggleButton dontShowAgainToggle_ { "Don't show this again" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WelcomePopup)
};

} // namespace play

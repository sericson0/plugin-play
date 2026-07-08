#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"
#include "../Plugins/PluginScanner.h"
#include "../Setup/VirtualCable.h"
#include "../Theme.h"
#include "ChainView.h"
#include "PluginWindow.h"

namespace play
{

//==============================================================================
/** Stereo peak meter bar (two thin horizontal bars, colour-coded by level)
    with peak-hold ticks and a latching clip light (click the meter to reset). */
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (const juce::String& labelText) : label (labelText) {}

    /** Feed the latest held peaks (linear gain); called from the UI timer. */
    void pushLevels (float left, float right);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String label;
    float display[2]  { 0.0f, 0.0f };
    float peakHold[2] { 0.0f, 0.0f };
    int   holdFrames[2] { 0, 0 };
    bool  clipped = false;
};

//==============================================================================
class MainComponent : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    MainComponent (AudioEngine&, PluginScanner&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void showAddPluginMenu (juce::Point<int> screenPosition);
    void showScanMenu();
    void addScanFolder();
    void showHelp();
    void showPresetsMenu();
    void promptSavePreset();
    void savePresetTo (const juce::File&);
    void updateKillButton();
    void updateLimiterButton();
    void openPluginEditor (int slotIndex);
    void setPluginFloating (int slotIndex, bool shouldFloat);
    bool isSlotFloating (int slotIndex) const;
    void closePluginWindow (juce::AudioProcessorGraph::NodeID);
    void updateStatusText();
    void updateScanButton();

    // Collapsible audio device area: collapsed keeps the input/output device
    // selectors; expanded adds channel selection plus driver / rate / buffer.
    void setAudioExpanded (bool shouldExpand);
    void updateAudioToggle();

    // Inline input/output/sample-rate selection (the "device bar" below the meters).
    void buildDeviceSelectors();
    void refreshDeviceTypes();
    void refreshChannelSelectors();
    void refreshSampleRates();
    void refreshBufferSizes();
    void applyDeviceSelection();
    void applyChannelSelection (bool isInput);
    void applySampleRate (double rate);
    void applyBufferSize (int frames);
    double recommendedSampleRate() const;
    void checkSampleRate();

    AudioEngine& engine;
    PluginScanner& scanner;

    juce::TextButton scanButton       { "SCAN PLUGINS" };
    juce::TextButton cableButton      { "VIRTUAL CABLE" };
    juce::TextButton helpButton       { "HELP" };
    juce::TextButton presetsButton    { "PRESETS" };
    juce::TextButton killButton       { "FX ON" };
    juce::TextButton limiterButton    { "LIMITER" };
    juce::TextButton addPluginButton  { "+  Add Plugin" };
    juce::TextButton audioToggleButton;
    LevelMeter inputMeter  { "IN" };
    LevelMeter outputMeter { "OUT" };

    juce::Label      inputLabel  { {}, "INPUT" };
    juce::Label      outputLabel { {}, "OUTPUT" };
    juce::Label      driverLabel { {}, "DRIVER" };
    juce::Label      rateLabel   { {}, "RATE" };
    juce::Label      bufferLabel { {}, "BUFFER" };
    juce::ComboBox   inputSelector;
    juce::ComboBox   outputSelector;
    juce::ComboBox   inputChannelSelector;
    juce::ComboBox   outputChannelSelector;
    juce::ComboBox   deviceTypeSelector;
    juce::ComboBox   sampleRateSelector;
    juce::ComboBox   bufferSizeSelector;
    juce::TextButton testButton   { "TEST" };
    juce::Label      rateHint;
    juce::Rectangle<int> deviceBarBounds;
    juce::Rectangle<int> toolbarBounds;
    bool audioExpanded = true;
    bool updatingSelectors = false;
    bool autoRate = true;                 // match the source's rate automatically
    double lastAutoAppliedRate = 0.0;

    // "Auto — match source" lives as the first item in the rate dropdown. Sample
    // rates in Hz are always well above this, so the id can't collide with one.
    static constexpr int autoRateItemId = 1;
    std::unique_ptr<juce::PropertiesFile> uiPrefs;

    juce::Viewport viewport;
    ChainView chainView { engine };

    juce::Label statusLabel;

    std::map<juce::uint32, std::unique_ptr<PluginWindow>> pluginWindows;

    // Desired "pin on top" state per plugin node, kept even while its editor is
    // closed so the FLOAT toggle survives and applies the next time it opens.
    std::map<juce::uint32, bool> floatingSlots;

    juce::TooltipWindow tooltipWindow { this };

    std::unique_ptr<juce::FileChooser> scanFolderChooser;

    int timerTicks = 0;

    // Silence watchdog: warn if a running input has been flat-zero for a while.
    int  silentTicks   = 0;
    bool noInputSignal = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace play

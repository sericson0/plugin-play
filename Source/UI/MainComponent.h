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
class LevelMeter : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    explicit LevelMeter (const juce::String& labelText) : label (labelText)
    {
        setTooltip ("Peak level (green safe / amber hot / red near clip). "
                    "Click to reset the clip indicator.");
    }

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
    float clipGlowPhase = 0.0f;   // drives the pulsing glow behind a lit clip lamp
};

//==============================================================================
/** A text button that pulses a coloured border to demand attention while in an
    "alert" state (e.g. master FX bypassed, or the safety limiter switched off). */
class AlertButton : public juce::TextButton,
                    private juce::Timer
{
public:
    explicit AlertButton (const juce::String& text) : juce::TextButton (text) {}

    /** Turn the pulsing alert ring on or off, in the given colour. */
    void setAlert (bool shouldAlert, juce::Colour colour)
    {
        alertColour = colour;

        if (shouldAlert == alerting)
        {
            repaint();
            return;
        }

        alerting = shouldAlert;

        if (alerting)
            startTimerHz (30);
        else
            stopTimer();

        repaint();
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (! alerting)
            return;

        // A pulsing ring drawn on top of the button's normal background.
        const float glow = 0.45f + 0.55f * (0.5f + 0.5f * std::sin (phase));
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (alertColour.withAlpha (glow));
        g.drawRoundedRectangle (bounds, 5.0f, 2.0f);
    }

private:
    void timerCallback() override
    {
        phase += 0.28f;
        repaint();
    }

    bool alerting = false;
    float phase = 0.0f;
    juce::Colour alertColour { juce::Colours::red };
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
    void refreshInputSelector();
    void refreshChannelSelectors();
    void refreshSampleRates();
    void refreshBufferSizes();
    void applyInputSelection();
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
    AlertButton      killButton       { "FX ON" };
    AlertButton      limiterButton    { "LIMITER" };
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
    juce::Label      inputChannelLabel  { {}, "INPUT PAIR" };
    juce::Label      outputChannelLabel { {}, "OUTPUT PAIR" };
    juce::ComboBox   inputChannelSelector;
    juce::ComboBox   outputChannelSelector;
    juce::ComboBox   deviceTypeSelector;
    juce::ComboBox   sampleRateSelector;
    juce::ComboBox   bufferSizeSelector;
    juce::TextButton testButton   { "TEST OUTPUT" };
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

    // The INPUT dropdown lists audio input devices (ids 1..N) followed by running
    // apps that can be captured driverlessly (ids captureItemBase + index into
    // captureSources). The base sits well above any plausible device count so the
    // two id ranges can't collide.
    static constexpr int captureItemBase = 10000;
    std::vector<AudioSource> captureSources;

    juce::Viewport viewport;
    ChainView chainView { engine };

    juce::Label statusLabel;
    juce::Label cpuLabel;   // right-aligned CPU readout, tinted amber/red under load

    std::map<juce::uint32, std::unique_ptr<PluginWindow>> pluginWindows;

    // Desired "pin on top" state per plugin node, kept even while its editor is
    // closed so the FLOAT toggle survives and applies the next time it opens.
    std::map<juce::uint32, bool> floatingSlots;

    juce::TooltipWindow tooltipWindow { this };

    std::unique_ptr<juce::FileChooser> scanFolderChooser;

    int timerTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace play

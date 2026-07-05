#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"
#include "../Plugins/PluginScanner.h"
#include "../Theme.h"
#include "ChainView.h"
#include "PluginWindow.h"

namespace play
{

//==============================================================================
/** Stereo peak meter bar (two thin horizontal bars, colour-coded by level). */
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (const juce::String& labelText) : label (labelText) {}

    /** Feed the latest held peaks (linear gain); called from the UI timer. */
    void pushLevels (float left, float right);

    void paint (juce::Graphics&) override;

private:
    juce::String label;
    float display[2] { 0.0f, 0.0f };
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

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void showAddPluginMenu (juce::Point<int> screenPosition);
    void showAudioSettings();
    void openPluginEditor (int slotIndex);
    void closePluginWindow (juce::AudioProcessorGraph::NodeID);
    void updateStatusText();
    void updateScanButton();

    AudioEngine& engine;
    PluginScanner& scanner;

    juce::TextButton settingsButton { "AUDIO SETTINGS" };
    juce::TextButton scanButton     { "SCAN PLUGINS" };
    LevelMeter inputMeter  { "IN" };
    LevelMeter outputMeter { "OUT" };

    juce::Viewport viewport;
    ChainView chainView { engine };

    juce::Label statusLabel;

    std::map<juce::uint32, std::unique_ptr<PluginWindow>> pluginWindows;

    int timerTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace play

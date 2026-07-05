#pragma once

#include <JuceHeader.h>

namespace play
{

//==============================================================================
/**
    Maintains the list of installed VST3 plugins.

    Scanning runs on a background thread over the standard VST3 directories.
    A dead-man's-pedal file blacklists any plugin that crashed a previous scan.
    The list is persisted in the app's properties file.

    Broadcasts a change message when scanning finishes (and as entries appear).
*/
class PluginScanner : public juce::ChangeBroadcaster,
                      private juce::Thread
{
public:
    PluginScanner (juce::AudioPluginFormatManager&, juce::PropertiesFile&);
    ~PluginScanner() override;

    void startScan();
    bool isScanning() const noexcept { return isThreadRunning(); }

    juce::String getProgressText() const;

    juce::KnownPluginList knownPlugins;

private:
    void run() override;
    void saveList();

    juce::AudioPluginFormatManager& formatManager;
    juce::PropertiesFile& properties;

    mutable juce::CriticalSection progressLock;
    juce::String progressText;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PluginScanner)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginScanner)
};

} // namespace play

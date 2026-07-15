#pragma once

#include <JuceHeader.h>

namespace play
{

//==============================================================================
/**
    Maintains the list of installed plugins — VST3 on every platform, plus
    Audio Unit on macOS (whichever formats the build hosts).

    Scanning runs on a background thread over each format's standard directories.
    Each candidate file is examined in a separate worker process (see
    OutOfProcessScanner), so a plugin that crashes or hangs while being
    catalogued is blacklisted instead of taking the app down. A dead-man's-pedal
    file remains as a backstop against the app process itself dying mid-scan.
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

    //==============================================================================
    /** Extra folders to scan on top of the OS's default VST3 locations, for
        plugins kept in non-standard places. Persisted in the properties file. */
    juce::StringArray getUserScanPaths() const;
    void addUserScanPath (const juce::File&);
    void removeUserScanPath (const juce::String& path);

    juce::KnownPluginList knownPlugins;

private:
    void run() override;
    void saveList();
    void loadUserScanPaths();

    juce::AudioPluginFormatManager& formatManager;
    juce::PropertiesFile& properties;

    mutable juce::CriticalSection progressLock;
    juce::String progressText;

    mutable juce::CriticalSection pathLock;
    juce::StringArray userScanPaths;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PluginScanner)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginScanner)
};

} // namespace play

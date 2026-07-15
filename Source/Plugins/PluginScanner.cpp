#include "PluginScanner.h"
#include "OutOfProcessScanner.h"

namespace play
{

static juce::File getDeadMansPedalFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("PluginPlay")
             .getChildFile ("scan-crashed.txt");
}

//==============================================================================
PluginScanner::PluginScanner (juce::AudioPluginFormatManager& fm, juce::PropertiesFile& props)
    : juce::Thread ("VST3 scan"),
      formatManager (fm),
      properties (props)
{
    // Examine each candidate plugin in a worker process, so a plugin that crashes
    // or hangs while being catalogued can't take the app down with it (the first
    // launch runs a full automatic scan). See OutOfProcessScanner.
    knownPlugins.setCustomScanner (std::make_unique<OutOfProcessScanner>());

    if (auto savedList = properties.getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*savedList);

    loadUserScanPaths();

    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (knownPlugins,
                                                                       getDeadMansPedalFile());
}

PluginScanner::~PluginScanner()
{
    stopThread (10000);
}

void PluginScanner::startScan()
{
    if (! isThreadRunning())
        startThread();
}

juce::String PluginScanner::getProgressText() const
{
    const juce::ScopedLock lock (progressLock);
    return progressText;
}

//==============================================================================
void PluginScanner::loadUserScanPaths()
{
    const juce::ScopedLock lock (pathLock);
    userScanPaths.clear();
    userScanPaths.addTokens (properties.getValue ("customScanPaths"), "\n", "");
    userScanPaths.removeEmptyStrings();
}

juce::StringArray PluginScanner::getUserScanPaths() const
{
    const juce::ScopedLock lock (pathLock);
    return userScanPaths;
}

void PluginScanner::addUserScanPath (const juce::File& folder)
{
    if (! folder.isDirectory())
        return;

    {
        const juce::ScopedLock lock (pathLock);
        userScanPaths.addIfNotAlreadyThere (folder.getFullPathName());
    }

    properties.setValue ("customScanPaths", getUserScanPaths().joinIntoString ("\n"));
    properties.saveIfNeeded();
}

void PluginScanner::removeUserScanPath (const juce::String& path)
{
    {
        const juce::ScopedLock lock (pathLock);
        userScanPaths.removeString (path);
    }

    properties.setValue ("customScanPaths", getUserScanPaths().joinIntoString ("\n"));
    properties.saveIfNeeded();
}

void PluginScanner::run()
{
    auto pedalFile = getDeadMansPedalFile();
    pedalFile.getParentDirectory().createDirectory();

    const auto userPaths = getUserScanPaths();

    // Scan every plugin format the host was built for — VST3 everywhere, plus
    // AudioUnit on macOS. addDefaultFormatsToManager only registers the formats
    // enabled at compile time (JUCE_PLUGINHOST_VST3 / _AU), so iterating the
    // manager's formats is already exactly the right set per platform.
    for (auto* format : formatManager.getFormats())
    {
        if (format == nullptr || threadShouldExit())
            break;

        // Default OS locations for this format, plus any user-added folders. The
        // extra folders are only meaningful for file-based formats (VST3); the AU
        // scanner enumerates registered components and simply ignores non-matches.
        auto searchPaths = format->getDefaultLocationsToSearch();
        for (const auto& path : userPaths)
            searchPaths.addIfNotAlreadyThere (juce::File (path));

        // allowAsync = true so AUv3 / plugins needing asynchronous instantiation
        // are still catalogued; the dead-man's-pedal blacklists any that crash us.
        juce::PluginDirectoryScanner scanner (knownPlugins, *format,
                                              searchPaths,
                                              true, pedalFile, true);

        juce::String currentName;

        while (! threadShouldExit() && scanner.scanNextFile (true, currentName))
        {
            {
                const juce::ScopedLock lock (progressLock);
                progressText = currentName;
            }
            sendChangeMessage();
        }
    }

    {
        const juce::ScopedLock lock (progressLock);
        progressText.clear();
    }

    juce::MessageManager::callAsync ([safeThis = juce::WeakReference<PluginScanner> (this)]
    {
        if (safeThis != nullptr)
        {
            safeThis->saveList();
            safeThis->sendChangeMessage();
        }
    });
}

void PluginScanner::saveList()
{
    if (auto xml = knownPlugins.createXml())
        properties.setValue ("knownPlugins", xml.get());

    properties.saveIfNeeded();
}

} // namespace play

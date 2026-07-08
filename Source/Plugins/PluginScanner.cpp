#include "PluginScanner.h"

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
    juce::AudioPluginFormat* vst3Format = nullptr;

    for (auto* format : formatManager.getFormats())
        if (format->getName() == "VST3")
            vst3Format = format;

    if (vst3Format == nullptr)
        return;

    auto pedalFile = getDeadMansPedalFile();
    pedalFile.getParentDirectory().createDirectory();

    // Default OS locations plus any user-added folders.
    auto searchPaths = vst3Format->getDefaultLocationsToSearch();
    for (const auto& path : getUserScanPaths())
        searchPaths.addIfNotAlreadyThere (juce::File (path));

    juce::PluginDirectoryScanner scanner (knownPlugins, *vst3Format,
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

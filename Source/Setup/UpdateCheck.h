#pragma once

#include <JuceHeader.h>

namespace play
{

//==============================================================================
/**
    Checks GitHub for a newer released version of Plugin Play.

    The check hits the public releases API for the newest published release,
    compares its tag against the running version, and hands back the installer
    asset's download URL so the UI can send the user straight to the new setup
    exe (falling back to the releases page if the release carries no exe).
*/
namespace UpdateCheck
{
    /** Public API endpoint describing the newest published release. */
    inline const juce::String latestReleaseApi {
        "https://api.github.com/repos/sericson0/plugin-play/releases/latest" };

    /** Browser fallback if the API response carries no installer asset. */
    inline const juce::String releasesPage {
        "https://github.com/sericson0/plugin-play/releases/latest" };

    struct Result
    {
        bool ok = false;                // the check itself succeeded
        bool updateAvailable = false;   // a strictly newer version exists
        juce::String latestVersion;     // e.g. "1.2.0" (any leading 'v' stripped)
        juce::String downloadUrl;       // installer asset, or the releases page
    };

    /** True if candidate is a strictly newer dotted version than current.
        A leading 'v' is tolerated and missing fields read as 0 ("1.1" == "1.1.0"). */
    bool isNewerVersion (const juce::String& current, const juce::String& candidate);

    /** Blocking network fetch + parse — call off the message thread. */
    Result fetchLatest (const juce::String& currentVersion);

    /** Runs fetchLatest on a background thread, then delivers the result to
        onDone on the message thread. */
    void checkAsync (const juce::String& currentVersion,
                     std::function<void (const Result&)> onDone);
}

} // namespace play

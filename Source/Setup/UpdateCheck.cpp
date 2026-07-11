#include "UpdateCheck.h"

namespace play::UpdateCheck
{

static juce::Array<int> parseVersion (const juce::String& version)
{
    auto trimmed = version.trim();
    while (trimmed.startsWithIgnoreCase ("v"))
        trimmed = trimmed.substring (1);

    juce::Array<int> numbers;
    for (const auto& part : juce::StringArray::fromTokens (trimmed, ".", {}))
        numbers.add (part.getIntValue());

    return numbers;
}

bool isNewerVersion (const juce::String& current, const juce::String& candidate)
{
    const auto a = parseVersion (current);
    const auto b = parseVersion (candidate);

    for (int i = 0; i < juce::jmax (a.size(), b.size()); ++i)
    {
        const int av = i < a.size() ? a[i] : 0;
        const int bv = i < b.size() ? b[i] : 0;

        if (av != bv)
            return bv > av;
    }

    return false;
}

Result fetchLatest (const juce::String& currentVersion)
{
    Result result;

    // GitHub's API rejects requests without a User-Agent, so always send one.
    auto stream = juce::URL (latestReleaseApi).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withExtraHeaders ("User-Agent: PluginPlay\r\nAccept: application/vnd.github+json")
            .withConnectionTimeoutMs (10000));

    if (stream == nullptr)
        return result;

    const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
    const auto tag  = json.getProperty ("tag_name", {}).toString();

    if (tag.isEmpty())   // error payload, rate-limited, or not JSON at all
        return result;

    result.ok              = true;
    result.latestVersion   = tag.startsWithIgnoreCase ("v") ? tag.substring (1) : tag;
    result.updateAvailable = isNewerVersion (currentVersion, tag);
    result.downloadUrl     = releasesPage;

    // Point straight at the installer when the release carries one.
    if (auto* assets = json.getProperty ("assets", {}).getArray())
    {
        for (const auto& asset : *assets)
        {
            const auto name = asset.getProperty ("name", {}).toString();
            const auto url  = asset.getProperty ("browser_download_url", {}).toString();

            if (name.endsWithIgnoreCase (".exe") && url.isNotEmpty())
            {
                result.downloadUrl = url;
                break;
            }
        }
    }

    return result;
}

void checkAsync (const juce::String& currentVersion, std::function<void (const Result&)> onDone)
{
    juce::Thread::launch ([currentVersion, onDone = std::move (onDone)]
    {
        const auto result = fetchLatest (currentVersion);

        juce::MessageManager::callAsync ([onDone, result] { onDone (result); });
    });
}

} // namespace play::UpdateCheck

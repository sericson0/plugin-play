#include "OutOfProcessScanner.h"

#include <condition_variable>
#include <mutex>
#include <queue>

namespace play
{

// The unique marker in a worker launch's command line ("--pluginplayscan:<pipe>").
static const char* const scanWorkerID = "pluginplayscan";

// A plugin that takes this long to describe itself is treated as hung: its worker
// is killed and the file blacklisted so the next scan skips it. Generous, because
// a first scan of a big sampler/suite can be legitimately slow.
static constexpr double perPluginTimeoutMs = 90000.0;

//==============================================================================
// Parent side: one live worker process, replaced whenever it dies or hangs.
// (Adapted from the Superprocess in JUCE's AudioPluginHost.)
class OutOfProcessScanner::Subprocess final : private juce::ChildProcessCoordinator
{
public:
    Subprocess()
    {
        launched = launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile),
                                        scanWorkerID, 0, 0);
    }

    bool isLaunched() const noexcept { return launched; }

    enum class State
    {
        timeout,
        gotResult,
        connectionLost,
    };

    struct Response
    {
        State state;
        std::unique_ptr<juce::XmlElement> xml;
    };

    /** Waits briefly for the worker's reply. Returns `timeout` after 50 ms so the
        caller can poll its own deadline and thread-exit flag between waits. */
    Response getResponse()
    {
        std::unique_lock<std::mutex> lock { mutex };

        if (! condvar.wait_for (lock, std::chrono::milliseconds { 50 },
                                [&] { return gotResult || connectionLost; }))
            return { State::timeout, nullptr };

        const auto state = connectionLost ? State::connectionLost : State::gotResult;
        connectionLost = false;
        gotResult = false;

        return { state, std::move (resultXml) };
    }

    using juce::ChildProcessCoordinator::sendMessageToWorker;

private:
    void handleMessageFromWorker (const juce::MemoryBlock& mb) override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        resultXml = juce::parseXML (mb.toString());
        gotResult = true;
        condvar.notify_one();
    }

    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        connectionLost = true;
        condvar.notify_one();
    }

    std::mutex mutex;
    std::condition_variable condvar;

    std::unique_ptr<juce::XmlElement> resultXml;
    bool connectionLost = false;
    bool gotResult = false;
    bool launched = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Subprocess)
};

//==============================================================================
OutOfProcessScanner::OutOfProcessScanner() = default;
OutOfProcessScanner::~OutOfProcessScanner() = default;

bool OutOfProcessScanner::ensureSubprocess()
{
    if (subprocessUnavailable)
        return false;

    if (subprocess == nullptr)
    {
        subprocess = std::make_unique<Subprocess>();

        if (! subprocess->isLaunched())
        {
            // Couldn't start a worker at all — that's our infrastructure failing,
            // not a plugin. Scan in-process for the rest of this session rather
            // than blacklisting innocent files (and rather than retrying the
            // launch once per file).
            subprocess = nullptr;
            subprocessUnavailable = true;
            return false;
        }
    }

    return true;
}

bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format,
                                              juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    if (! ensureSubprocess())
    {
        format.findAllTypesForFile (result, fileOrIdentifier);
        return true;
    }

    juce::MemoryBlock block;
    juce::MemoryOutputStream stream { block, true };
    stream.writeString (format.getName());
    stream.writeString (fileOrIdentifier);

    if (! subprocess->sendMessageToWorker (block))
    {
        // The pipe died before this file was even sent — an infrastructure
        // failure, not this plugin's fault. Same fallback as a failed launch.
        subprocess = nullptr;
        subprocessUnavailable = true;
        format.findAllTypesForFile (result, fileOrIdentifier);
        return true;
    }

    const auto deadline = juce::Time::getMillisecondCounterHiRes() + perPluginTimeoutMs;

    for (;;)
    {
        // The scan is being cancelled (app quitting / scan thread stopped): stop
        // waiting. Returning true avoids blacklisting the innocent in-flight file.
        if (shouldExit() || juce::Thread::currentThreadShouldExit())
            return true;

        auto response = subprocess->getResponse();

        if (response.state == Subprocess::State::timeout)
        {
            // Hung plugin: kill the worker (the next file gets a fresh one) and
            // report failure, which blacklists this file.
            if (juce::Time::getMillisecondCounterHiRes() >= deadline)
            {
                subprocess = nullptr;
                return false;
            }

            continue;
        }

        if (response.xml != nullptr)
        {
            for (const auto* item : response.xml->getChildIterator())
            {
                auto desc = std::make_unique<juce::PluginDescription>();

                if (desc->loadFromXml (*item))
                    result.add (std::move (desc));
            }
        }

        if (response.state == Subprocess::State::gotResult)
            return true;

        // Connection lost: the worker crashed examining this file. Blacklist it;
        // the next file will get a fresh worker.
        subprocess = nullptr;
        return false;
    }
}

void OutOfProcessScanner::scanFinished()
{
    subprocess = nullptr;            // no idle worker process left between scans
    subprocessUnavailable = false;   // a later scan retries out-of-process
}

//==============================================================================
// Worker side: a headless Plugin Play that examines plugin files for the parent.
// (Adapted from the PluginScannerSubprocess in JUCE's AudioPluginHost.)
class ScanWorker final : public juce::ChildProcessWorker,
                         private juce::AsyncUpdater
{
public:
    ScanWorker()
    {
        juce::addDefaultFormatsToManager (formatManager);
    }

    ~ScanWorker() override
    {
        cancelPendingUpdate();
    }

private:
    void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
    {
        if (mb.isEmpty())
            return;

        const std::lock_guard<std::mutex> lock (mutex);

        // A format whose plugins need the message thread unblocked during creation
        // can be scanned right here on the IPC thread; anything else is deferred to
        // the message thread (this callback arrives on a background thread).
        if (const auto results = doScan (mb); ! results.isEmpty())
        {
            sendResults (results);
        }
        else
        {
            pendingBlocks.emplace (mb);
            triggerAsyncUpdate();
        }
    }

    void handleConnectionLost() override
    {
        // The parent went away (finished its scan, or died): this process has no
        // other purpose, so exit.
        juce::JUCEApplicationBase::quit();
    }

    void handleAsyncUpdate() override
    {
        for (;;)
        {
            const std::lock_guard<std::mutex> lock (mutex);

            if (pendingBlocks.empty())
                return;

            sendResults (doScan (pendingBlocks.front()));
            pendingBlocks.pop();
        }
    }

    juce::OwnedArray<juce::PluginDescription> doScan (const juce::MemoryBlock& block)
    {
        juce::MemoryInputStream stream { block, false };
        const auto formatName = stream.readString();
        const auto identifier = stream.readString();

        juce::PluginDescription pd;
        pd.fileOrIdentifier = identifier;
        pd.uniqueId = pd.deprecatedUid = 0;

        const auto matchingFormat = [&]() -> juce::AudioPluginFormat*
        {
            for (auto* format : formatManager.getFormats())
                if (format->getName() == formatName)
                    return format;

            return nullptr;
        }();

        juce::OwnedArray<juce::PluginDescription> results;

        if (matchingFormat != nullptr
            && (juce::MessageManager::getInstance()->isThisTheMessageThread()
                || matchingFormat->requiresUnblockedMessageThreadDuringCreation (pd)))
        {
            matchingFormat->findAllTypesForFile (results, identifier);
        }

        return results;
    }

    void sendResults (const juce::OwnedArray<juce::PluginDescription>& results)
    {
        juce::XmlElement xml ("LIST");

        for (const auto* desc : results)
            xml.addChildElement (desc->createXml().release());

        const auto str = xml.toString();
        sendMessageToCoordinator ({ str.toRawUTF8(), str.getNumBytesAsUTF8() });
    }

    std::mutex mutex;
    std::queue<juce::MemoryBlock> pendingBlocks;
    juce::AudioPluginFormatManager formatManager;
};

//==============================================================================
bool isScanWorkerCommandLine (const juce::String& commandLine)
{
    return commandLine.contains ("--" + juce::String (scanWorkerID) + ":");
}

std::unique_ptr<juce::ChildProcessWorker> createScanWorkerIfNeeded (const juce::String& commandLine)
{
    if (! isScanWorkerCommandLine (commandLine))
        return nullptr;

   #if JUCE_MAC
    juce::Process::setDockIconVisible (false);   // a helper process, not a second app
   #endif

    auto worker = std::make_unique<ScanWorker>();

    // A malformed or expired pipe argument means there's no parent to serve: quit
    // straight away. (Still return the worker — the command line says this process
    // is a scanner, so it must never fall through to normal app startup.)
    if (! worker->initialiseFromCommandLine (commandLine, scanWorkerID))
        juce::JUCEApplicationBase::quit();

    return worker;
}

} // namespace play

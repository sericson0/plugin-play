#pragma once

#include <JuceHeader.h>

namespace play
{

//==============================================================================
/**
    Out-of-process plugin scanning.

    Cataloguing a plugin means loading its binary and asking it to describe
    itself — and if the plugin crashes or hangs doing that, an in-process scan
    takes the whole app down with it (the very first launch runs a full
    automatic scan, so one bad plugin used to mean a crash on first run).

    Instead, each candidate file is examined by a headless copy of Plugin Play
    launched as a worker process: a crash costs only that worker (the file is
    blacklisted and the scan moves on), a hang is bounded by a timeout, and the
    app itself never dies. The dead-man's-pedal file stays in place as a second
    line of defence for the app process itself.
*/

/** True when this command line marks the process as a scan worker (the special
    "--<id>:<pipe>" argument ChildProcessCoordinator::launchWorkerProcess
    generates). The app uses this to exempt worker launches from the
    single-instance check — they're spawned BY the running instance. */
bool isScanWorkerCommandLine (const juce::String& commandLine);

/** When the command line marks this process as a scan worker, connects back to
    the parent and returns the live worker. The caller must keep it alive and
    skip ALL normal startup (no UI, no engine, no session): the worker just
    examines the plugin files it's sent and quits the app when the parent
    disconnects. Returns nullptr for a normal launch. */
std::unique_ptr<juce::ChildProcessWorker> createScanWorkerIfNeeded (const juce::String& commandLine);

//==============================================================================
/** The parent-side scanner plugged into KnownPluginList: forwards each file to
    the worker process and turns a worker crash or timeout into a blacklisting
    (returning false) instead of a crash. If a worker can't be launched at all
    (an infrastructure failure, not a plugin's fault), it falls back to scanning
    in-process for the rest of that scan rather than blacklisting everything. */
class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    OutOfProcessScanner();
    ~OutOfProcessScanner() override;

    bool findPluginTypesFor (juce::AudioPluginFormat& format,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;

    void scanFinished() override;

private:
    class Subprocess;

    bool ensureSubprocess();

    std::unique_ptr<Subprocess> subprocess;
    bool subprocessUnavailable = false;   // worker launch failed: scan in-process this session

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutOfProcessScanner)
};

} // namespace play

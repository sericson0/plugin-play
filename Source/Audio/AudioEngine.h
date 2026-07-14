#pragma once

#include <JuceHeader.h>
#include "AppRouting.h"
#include "LoopbackCapture.h"

namespace play
{

class MasterProcessor;
class PluginNode;
class CaptureSourceProcessor;

//==============================================================================
/** One entry in the serial FX chain. */
struct PluginSlotInfo
{
    juce::AudioProcessorGraph::NodeID nodeID;
    juce::PluginDescription description;
    bool bypassed = false;
};

//==============================================================================
/**
    Owns the audio devices and the processing graph:

        input device -> [plugin 0] -> [plugin 1] -> ... -> output device

    All chain mutations happen on the message thread; AudioProcessorGraph
    handles swapping the render sequence safely under the hood.

    Broadcasts a change message whenever the chain or devices change.
*/
class AudioEngine : public juce::ChangeBroadcaster,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;

    //==============================================================================
    int getNumPlugins() const                       { return (int) slots.size(); }
    const PluginSlotInfo& getSlot (int index) const { return slots[(size_t) index]; }
    juce::AudioProcessor* getProcessor (int index) const;

    void addPlugin (const juce::PluginDescription&,
                    std::function<void (const juce::String& error)> onDone = nullptr);
    void removePlugin (int index);
    void movePlugin (int fromIndex, int toIndex);
    void setBypassed (int index, bool shouldBeBypassed);
    void clearChain();

    /** Kill switch: crossfades the whole chain to the dry input at the master
        node, so the effects drop out (and come back) without a click. */
    void setMasterBypass (bool shouldBypass);
    bool isMasterBypassed() const noexcept          { return masterBypassed; }

    /** Brickwall safety limiter on the output; on by default. Persisted with
        the session. */
    void setLimiterEnabled (bool shouldLimit);
    bool isLimiterEnabled() const noexcept          { return limiterEnabled; }

    //==============================================================================
    /** ASIO device enumeration is DEFERRED on Windows: some third-party ASIO drivers
        block scanForDevices() on the message thread when their hardware is absent,
        which would freeze the whole app during startup. Only WASAPI/DirectSound are
        registered up front; ASIO is added — and scanned — on demand the first time the
        user picks it. Returns true if ASIO is available (and now selected) afterwards. */
    bool ensureAsioEnabled();
    bool isAsioEnabled() const noexcept             { return asioEnabled; }

    //==============================================================================
    /** Running apps that can be picked as an input source (one entry per app). */
    std::vector<AudioSource> availableCaptureSources() const { return enumerateAudioSources(); }

    /** App-to-cable input. Picking an app redirects THAT app's audio output into the
        virtual cable (via AppRouting / per-app device routing) and points Plugin
        Play's input at the cable's recording endpoint — so the user just picks an
        app and we wire the cable for them, output stays on the same speakers, and no
        Windows sound-settings fiddling is needed. Returns "" on success, otherwise a
        human-readable error (no cable installed, or routing unsupported on this OS).
        This replaces the driverless loopback+mute path as the app-input method. */
    juce::String setRedirectedApp (juce::uint32 pid, const juce::String& exe);

    /** Stop redirecting: restore the app's output to the system default. Leaves the
        input device as-is (the caller sets a new device when switching away). */
    void clearRedirectedApp();

    bool isRedirectingApp() const noexcept          { return redirectedApp.isNotEmpty(); }
    juce::String redirectedAppName() const          { return redirectedApp; }

    /** True while an app redirect is active AND that app's process is still alive.
        The UI polls this to notice the redirected app quitting (audio would otherwise
        just go silent) so it can stop routing and tell the user. Returns true when not
        redirecting, so callers only act on a false result. */
    bool isRedirectedAppRunning() const;

    //==============================================================================
    // QUARANTINED: driverless process-loopback CAPTURE of an app, which master-mutes
    // the default render endpoint to kill the dry signal. Superseded by the app-to-
    // cable redirect above (it needs no muting and works with a single output). Kept
    // compiled but unused; gate on enableLoopbackCapture to revive.
    void setCaptureSource (juce::uint32 pid);
    void setDeviceInput();
    bool isCapturingInput() const noexcept          { return useCaptureInput; }
    juce::uint32 capturedSourcePid() const noexcept { return capture.targetPid(); }
    juce::String capturedSourceName() const         { return capturedExecutable; }

    //==============================================================================
    /** Restores the most recently removed plugin (state and position intact).
        Single level of undo. */
    bool canUndoRemove() const noexcept             { return lastRemovedSlot != nullptr; }
    void undoRemove();

    //==============================================================================
    /** Named chain presets, stored as XML files in the presets directory. */
    juce::File getPresetsDirectory() const;
    bool savePreset (const juce::File& presetFile);
    bool loadPreset (const juce::File& presetFile);

    /** Called just before a plugin node is destroyed, so the UI can close its window. */
    std::function<void (juce::AudioProcessorGraph::NodeID)> onPluginAboutToBeRemoved;

    /** Called after a session/preset restore if any plugins failed to load, so
        the UI can tell the user which ones went missing. */
    std::function<void (const juce::StringArray& failedPlugins)> onRestoreErrors;

    //==============================================================================
    /** Peak level since last call (linear gain); reading resets the held peak. */
    float readInputPeak (int channel)  { return inputPeaks [channel & 1].exchange (0.0f); }
    float readOutputPeak (int channel) { return outputPeaks[channel & 1].exchange (0.0f); }

    //==============================================================================
    /** Restores devices + chain from disk (or opens default devices on first run).
        Runs in stages spread over several message-loop turns (device open, then the
        app redirect, then one plugin per turn) so the window keeps painting and
        responding between the heavy steps instead of blocking in one long call. */
    void loadSession();
    void saveSession();

    /** False until the deferred startup loadSession() has begun; the UI shows a
        "starting" status instead of "no audio device" while this is false. */
    bool isSessionLoaded() const noexcept           { return sessionLoaded; }

    /** True while a session/preset restore is still instantiating plugins;
        restoringPluginName() names the one currently loading (for the status line). */
    bool isRestoringChain() const noexcept          { return restoringSession; }
    juce::String restoringPluginName() const        { return currentlyRestoring; }

private:
    //==============================================================================
    /** Wraps the AudioProcessorPlayer callback to capture in/out peak levels. */
    class MeterTap : public juce::AudioIODeviceCallback
    {
    public:
        MeterTap (juce::AudioIODeviceCallback& innerCallback, AudioEngine& ownerEngine)
            : inner (innerCallback), owner (ownerEngine) {}

        void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                               float* const* outputChannelData, int numOutputChannels,
                                               int numSamples,
                                               const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice* device) override { inner.audioDeviceAboutToStart (device); }
        void audioDeviceStopped() override                                  { inner.audioDeviceStopped(); }

    private:
        juce::AudioIODeviceCallback& inner;
        AudioEngine& owner;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    /** Coalesces bursts of chain edits into one save, then keeps autosaving
        periodically so plugin parameter tweaks survive a crash. */
    void scheduleSave();

    std::unique_ptr<juce::XmlElement> createSlotXml (int index) const;
    std::unique_ptr<juce::XmlElement> createChainXml (bool includeGhosts = true) const;

    void rebuildConnections();
    void connectNodes (juce::AudioProcessorGraph::NodeID source, juce::AudioProcessorGraph::NodeID dest);
    void restoreChainFromXml (std::shared_ptr<juce::XmlElement> chainXml, int slotIndex,
                              std::shared_ptr<juce::StringArray> failures, int generation);
    void insertPluginFromXml (std::unique_ptr<juce::XmlElement> slotXml, int index);

    /** The bypass/limiter wrapper hosting the plugin at a slot, or nullptr. */
    PluginNode* getPluginNode (int index) const;

    juce::File getSessionFile() const;
    double currentSampleRate() const;
    int currentBlockSize() const;

    /** Registers ONLY the device type the saved session opens with. Called before the
        device manager is initialised, for two reasons: it stops JUCE adding + scanning
        its full default set (which includes the hang-prone ASIO), and initialise()
        synchronously scans every registered type on the message thread, so each extra
        type registered here directly lengthens the startup freeze. No-op off Windows
        (JUCE's defaults are used there). */
    void registerSafeDeviceTypes();

    /** Adds + scans the remaining safe (non-ASIO) device types, one per timer turn,
        once startup is done. Each scan blocks the message thread for a moment, so
        they're spread across turns and wait for the chain restore to finish first. */
    void registerRemainingDeviceTypesWhenIdle();

    /** Device type name (e.g. "Windows Audio") the saved session opens with, or "". */
    juce::String savedDeviceTypeName() const;

    bool asioEnabled = false;

    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    MeterTap meterTap { player, *this };

    juce::AudioProcessorGraph::NodeID inputNodeID, outputNodeID, masterNodeID;
    juce::AudioProcessorGraph::NodeID captureSourceNodeID;
    MasterProcessor* master = nullptr;
    std::vector<PluginSlotInfo> slots;

    // App-to-cable redirect state: the exe currently routed into the cable (empty =
    // none) and the live PID we set the per-app override on (needed to clear it).
    juce::String redirectedApp;
    juce::uint32 redirectedPid = 0;

    void applyRedirect (juce::uint32 pid, const juce::String& exe, const juce::String& captureName);
    void restoreRedirectFromSession (const juce::XmlElement& session);
    void cleanupStaleRedirect();
    juce::File redirectMarkerFile() const;

    // Feature flag for the QUARANTINED loopback+mute capture path. Off: the engine
    // never mutes an endpoint and legacy mode="capture" sessions migrate to redirect.
    static constexpr bool enableLoopbackCapture = false;

    // Driverless process-loopback capture (quarantined). When useCaptureInput is true
    // the chain head is captureSourceNode (fed by `capture`) instead of device input.
    LoopbackCapture capture;
    bool useCaptureInput = false;
    double captureStartedRate = 0.0;
    // Executable of the current capture target, remembered so the source survives
    // a relaunch (the PID won't — we re-resolve the name to a live PID on load).
    juce::String capturedExecutable;

    void restoreInputSource (const juce::XmlElement& session);

    std::atomic<float> inputPeaks[2]  { 0.0f, 0.0f };
    std::atomic<float> outputPeaks[2] { 0.0f, 0.0f };

    bool restoringSession = false;
    // Name of the plugin a session/preset restore is currently instantiating (shown
    // in the status line); empty when no restore is in flight.
    juce::String currentlyRestoring;
    // False until loadSession() runs (startup defers it past the first paint); gates
    // every save so an early exit can't replace the saved session with an empty one.
    bool sessionLoaded = false;
    bool masterBypassed = false;
    bool limiterEnabled = true;

    // Bumped by every clearChain/loadPreset/loadSession. In-flight async plugin
    // instantiations from a superseded restore compare against this and bail, so
    // a new load can't have a previous load's late plugins appended to it.
    int restoreGeneration = 0;

    // One level of undo for plugin removal: the removed slot's serialized state
    // and the index it was at.
    std::unique_ptr<juce::XmlElement> lastRemovedSlot;
    int lastRemovedIndex = 0;

    // Slots from a restored session whose plugin failed to instantiate (missing DLL,
    // momentarily-locked file, not-yet-mounted drive, one-off init failure). Their
    // original XML is retained and re-emitted verbatim on every save — keyed by their
    // position in the restored chain — so a *transient* failure never permanently
    // deletes the plugin; the next launch retries it. Dropped the moment the user makes
    // a structural edit (add/remove/reorder), since they've then accepted the chain as
    // shown. Empty in the normal case, where createChainXml behaves exactly as before.
    struct GhostSlot
    {
        int originalIndex = 0;
        std::unique_ptr<juce::XmlElement> xml;
    };
    std::vector<GhostSlot> ghostSlots;
    void dropGhostSlots();

    JUCE_DECLARE_WEAK_REFERENCEABLE (AudioEngine)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace play

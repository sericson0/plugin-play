#include "AudioEngine.h"
#include "MasterProcessor.h"
#include "PluginNode.h"

namespace play
{

using Graph  = juce::AudioProcessorGraph;
using IONode = juce::AudioProcessorGraph::AudioGraphIOProcessor;

//==============================================================================
/** Graph head node for driverless capture mode: a 0-in / 2-out source that pulls
    the process-loopback FIFO in processBlock (lock-free, no allocation). It lives
    in the graph permanently but is only connected while capture is the input. */
class CaptureSourceProcessor : public juce::AudioProcessor
{
public:
    explicit CaptureSourceProcessor (LoopbackCapture& c)
        : juce::AudioProcessor (BusesProperties()
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          capture (c) {}

    const juce::String getName() const override        { return "Capture Source"; }
    void prepareToPlay (double, int) override           {}
    void releaseResources() override                    {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        capture.read (buffer.getArrayOfWritePointers(),
                      buffer.getNumChannels(), buffer.getNumSamples());
    }

    double getTailLengthSeconds() const override        { return 0.0; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    juce::AudioProcessorEditor* createEditor() override  { return nullptr; }
    bool hasEditor() const override                     { return false; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    LoopbackCapture& capture;
};

// Chain edits save after a short debounce; the long interval keeps autosaving
// so plugin parameter tweaks (which broadcast nothing) survive a crash.
static constexpr int saveDebounceMs     = 1000;
static constexpr int autosaveIntervalMs = 30000;

//==============================================================================
void AudioEngine::MeterTap::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                              float* const* outputChannelData, int numOutputChannels,
                                                              int numSamples,
                                                              const juce::AudioIODeviceCallbackContext& context)
{
    auto holdPeak = [numSamples] (std::atomic<float>& holder, const float* data)
    {
        auto range = juce::FloatVectorOperations::findMinAndMax (data, numSamples);
        auto peak  = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
        auto held  = holder.load (std::memory_order_relaxed);
        while (peak > held && ! holder.compare_exchange_weak (held, peak, std::memory_order_relaxed)) {}
    };

    for (int ch = 0; ch < juce::jmin (2, numInputChannels); ++ch)
        if (inputChannelData[ch] != nullptr)
            holdPeak (owner.inputPeaks[ch], inputChannelData[ch]);

    inner.audioDeviceIOCallbackWithContext (inputChannelData, numInputChannels,
                                            outputChannelData, numOutputChannels,
                                            numSamples, context);

    for (int ch = 0; ch < juce::jmin (2, numOutputChannels); ++ch)
        if (outputChannelData[ch] != nullptr)
            holdPeak (owner.outputPeaks[ch], outputChannelData[ch]);
}

//==============================================================================
AudioEngine::AudioEngine()
{
    juce::addDefaultFormatsToManager (formatManager);

    // Register the safe device types BEFORE anything initialises the manager, so JUCE
    // skips its default set (which eagerly scans ASIO and can hang on a flaky driver).
    registerSafeDeviceTypes();

    auto inNode  = graph.addNode (std::make_unique<IONode> (IONode::audioInputNode));
    auto outNode = graph.addNode (std::make_unique<IONode> (IONode::audioOutputNode));
    inputNodeID  = inNode->nodeID;
    outputNodeID = outNode->nodeID;

    // The master node (limiter + clickless bypass crossfade) always sits between
    // the last plugin and the output.
    auto masterProcessor = std::make_unique<MasterProcessor>();
    master = masterProcessor.get();
    masterNodeID = graph.addNode (std::move (masterProcessor))->nodeID;
    master->setLimiterEnabled (limiterEnabled);

    // Head node used only in driverless capture mode; disconnected otherwise.
    captureSourceNodeID = graph.addNode (std::make_unique<CaptureSourceProcessor> (capture))->nodeID;

    player.setProcessor (&graph);
    deviceManager.addAudioCallback (&meterTap);
    deviceManager.addChangeListener (this);

    startTimer (autosaveIntervalMs);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    saveSession();           // persists the redirect intent (mode="redirect" exe=...)

    // Restore any app we routed into the cable back to its normal output so it isn't
    // left silently playing into the cable after we exit. The saved session still
    // records the redirect, so relaunching Plugin Play re-applies it.
    if (redirectedApp.isNotEmpty())
    {
        AppRouting::clearAppOutput (redirectedPid);
        redirectMarkerFile().deleteFile();
    }

    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&meterTap);
    player.setProcessor (nullptr);
    capture.stop();          // join the capture thread and restore the endpoint mute
    graph.clear();
}

//==============================================================================
PluginNode* AudioEngine::getPluginNode (int index) const
{
    if (! juce::isPositiveAndBelow (index, (int) slots.size()))
        return nullptr;

    if (auto* node = graph.getNodeForId (slots[(size_t) index].nodeID))
        return dynamic_cast<PluginNode*> (node->getProcessor());

    return nullptr;
}

juce::AudioProcessor* AudioEngine::getProcessor (int index) const
{
    // Callers (editor windows, state save) want the actual plugin, not the
    // bypass/limiter wrapper that hosts it in the graph.
    if (auto* wrapper = getPluginNode (index))
        return &wrapper->getInner();

    return nullptr;
}

void AudioEngine::addPlugin (const juce::PluginDescription& description,
                             std::function<void (const juce::String&)> onDone)
{
    formatManager.createPluginInstanceAsync (description, currentSampleRate(), currentBlockSize(),
        [safeThis = juce::WeakReference<AudioEngine> (this), description, onDone]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (safeThis == nullptr)
                return;

            auto& self = *safeThis;

            if (instance == nullptr)
            {
                if (onDone != nullptr)
                    onDone (error);
                return;
            }

            instance->enableAllBuses();
            auto wrapper = std::make_unique<PluginNode> (std::move (instance));
            wrapper->setMasterBypass (self.masterBypassed);
            auto node = self.graph.addNode (std::move (wrapper));
            self.dropGhostSlots();   // a structural edit: accept the chain as it stands
            self.slots.push_back ({ node->nodeID, description, false });

            self.rebuildConnections();
            self.scheduleSave();
            self.sendChangeMessage();

            if (onDone != nullptr)
                onDone ({});
        });
}

void AudioEngine::removePlugin (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) slots.size()))
        return;

    dropGhostSlots();   // a structural edit: the user has accepted the pruned chain

    auto nodeID = slots[(size_t) index].nodeID;

    // Stash the removed slot (state + position) so the removal can be undone.
    lastRemovedSlot  = createSlotXml (index);
    lastRemovedIndex = index;

    if (onPluginAboutToBeRemoved != nullptr)
        onPluginAboutToBeRemoved (nodeID);

    slots.erase (slots.begin() + index);
    graph.removeNode (nodeID);

    rebuildConnections();
    scheduleSave();
    sendChangeMessage();
}

void AudioEngine::undoRemove()
{
    if (lastRemovedSlot == nullptr)
        return;

    dropGhostSlots();   // a structural edit

    auto slotXml = std::move (lastRemovedSlot);   // consume: single level of undo
    insertPluginFromXml (std::move (slotXml), lastRemovedIndex);
}

void AudioEngine::dropGhostSlots()
{
    ghostSlots.clear();
}

void AudioEngine::clearChain()
{
    // Supersede any restore still instantiating plugins in the background so its
    // late callbacks don't push_back into the now-cleared chain. That restore will
    // never reach its completion step, so reset its in-progress state here too —
    // otherwise restoringSession would stay true forever, gating every future save.
    ++restoreGeneration;
    restoringSession = false;
    currentlyRestoring.clear();
    ghostSlots.clear();

    while (! slots.empty())
        removePlugin ((int) slots.size() - 1);

    // A bulk clear (e.g. loading a preset) shouldn't leave a stray "undo" that
    // would resurrect a plugin from the old chain.
    lastRemovedSlot = nullptr;
}

void AudioEngine::movePlugin (int fromIndex, int toIndex)
{
    toIndex = juce::jlimit (0, (int) slots.size() - 1, toIndex);

    if (! juce::isPositiveAndBelow (fromIndex, (int) slots.size()) || fromIndex == toIndex)
        return;

    dropGhostSlots();   // a structural edit

    auto slot = slots[(size_t) fromIndex];
    slots.erase (slots.begin() + fromIndex);
    slots.insert (slots.begin() + toIndex, slot);

    rebuildConnections();
    scheduleSave();
    sendChangeMessage();
}

void AudioEngine::setBypassed (int index, bool shouldBeBypassed)
{
    if (! juce::isPositiveAndBelow (index, (int) slots.size()))
        return;

    slots[(size_t) index].bypassed = shouldBeBypassed;

    // The wrapper crossfades wet<->dry (latency-compensated), so toggling a
    // plugin never clicks, even a high-latency one.
    if (auto* wrapper = getPluginNode (index))
        wrapper->setUserBypass (shouldBeBypassed);

    scheduleSave();
    sendChangeMessage();
}

void AudioEngine::setMasterBypass (bool shouldBypass)
{
    if (masterBypassed == shouldBypass)
        return;

    masterBypassed = shouldBypass;

    // Master kill engages every wrapper's bypass at once; each crossfades to its
    // latency-aligned dry, so the whole chain drops out (and returns) seamlessly
    // without disturbing the per-slot bypass flags.
    for (int i = 0; i < (int) slots.size(); ++i)
        if (auto* wrapper = getPluginNode (i))
            wrapper->setMasterBypass (masterBypassed);

    sendChangeMessage();
}

#if JUCE_WINDOWS
// The safe (non-ASIO) Windows device types, in the order the DRIVER menu lists them.
static const char* const safeDeviceTypeNames[] = { "Windows Audio",
                                                   "Windows Audio (Exclusive Mode)",
                                                   "Windows Audio (Low Latency Mode)",
                                                   "DirectSound" };

static std::unique_ptr<juce::AudioIODeviceType> createSafeDeviceType (const juce::String& name)
{
    using Type = juce::AudioIODeviceType;

    if (name == "Windows Audio (Exclusive Mode)")
        return std::unique_ptr<Type> (Type::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::exclusive));
    if (name == "Windows Audio (Low Latency Mode)")
        return std::unique_ptr<Type> (Type::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::sharedLowLatency));
    if (name == "DirectSound")
        return std::unique_ptr<Type> (Type::createAudioIODeviceType_DirectSound());

    return std::unique_ptr<Type> (Type::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
}
#endif

juce::String AudioEngine::savedDeviceTypeName() const
{
    auto file = getSessionFile();
    auto session = juce::XmlDocument::parse (file);

    if (session == nullptr)
        session = juce::XmlDocument::parse (file.getSiblingFile ("session.bak"));

    if (session != nullptr)
        if (auto* devices = session->getChildByName ("DEVICES"))
            if (auto* setup = devices->getFirstChildElement())
                return setup->getStringAttribute ("deviceType");

    return {};
}

void AudioEngine::registerSafeDeviceTypes()
{
   #if JUCE_WINDOWS
    // Register only the one type the saved session opens with (default: shared
    // WASAPI). initialise() scans every registered type synchronously on the message
    // thread, so registering the full safe set here meant four device enumerations
    // before the window could respond — the bulk of the startup freeze. The rest of
    // the set is filled in shortly after startup by registerRemainingDeviceTypesWhen-
    // Idle(); ASIO stays strictly on-demand (ensureAsioEnabled). Registering anything
    // also stops createDeviceTypesIfNeeded() adding + scanning JUCE's default list.
    auto saved = savedDeviceTypeName();

    if (saved == "ASIO")   // never scanned at startup; the WASAPI fallback opens instead
        saved.clear();

    deviceManager.addAudioDeviceType (createSafeDeviceType (saved));
   #endif
}

void AudioEngine::registerRemainingDeviceTypesWhenIdle()
{
   #if JUCE_WINDOWS
    // One type per timer turn: WM_TIMER is only delivered after paint and input have
    // been serviced, so the window stays live between the scans (each of which blocks
    // the message thread for a moment). Waits out an in-flight chain restore so the
    // user's plugins come back before we spend time on driver lists.
    juce::Timer::callAfterDelay (250, [weak = juce::WeakReference<AudioEngine> (this)]
    {
        if (weak == nullptr)
            return;

        auto& self = *weak;

        if (self.restoringSession)
        {
            self.registerRemainingDeviceTypesWhenIdle();
            return;
        }

        for (auto* name : safeDeviceTypeNames)
        {
            bool present = false;
            for (auto* type : self.deviceManager.getAvailableDeviceTypes())
                if (type->getTypeName() == name)
                    { present = true; break; }

            if (present)
                continue;

            auto type = createSafeDeviceType (name);
            auto* raw = type.get();
            self.deviceManager.addAudioDeviceType (std::move (type));
            raw->scanForDevices();       // scanned before anyone can call getDeviceNames
            self.sendChangeMessage();    // the DRIVER dropdown picks up the new entry

            self.registerRemainingDeviceTypesWhenIdle();   // next type, next turn
            return;
        }
    });
   #endif
}

bool AudioEngine::ensureAsioEnabled()
{
   #if JUCE_WINDOWS
    if (! asioEnabled)
    {
        std::unique_ptr<juce::AudioIODeviceType> asio (juce::AudioIODeviceType::createAudioIODeviceType_ASIO());
        if (asio == nullptr)
            return false;

        deviceManager.addAudioDeviceType (std::move (asio));
        asioEnabled = true;

        // Scan ASIO now — user-initiated, so a slow/blocking driver only affects this
        // explicit action, never startup. (The type was just added, so getAvailable-
        // DeviceTypes returns it without triggering a full rescan of the others.)
        for (auto* type : deviceManager.getAvailableDeviceTypes())
            if (type->getTypeName() == "ASIO")
            {
                type->scanForDevices();
                break;
            }
    }

    deviceManager.setCurrentAudioDeviceType ("ASIO", true);
    scheduleSave();
    sendChangeMessage();
    return true;
   #else
    return false;
   #endif
}

void AudioEngine::setLimiterEnabled (bool shouldLimit)
{
    if (limiterEnabled == shouldLimit)
        return;

    limiterEnabled = shouldLimit;

    if (master != nullptr)
        master->setLimiterEnabled (limiterEnabled);

    scheduleSave();
    sendChangeMessage();
}

//==============================================================================
void AudioEngine::setCaptureSource (juce::uint32 targetPid)
{
    // Remember the target's executable so the choice survives a relaunch (the PID
    // won't). Resolved on the message thread from the same list the picker shows.
    capturedExecutable.clear();
    for (const auto& source : enumerateAudioSources())
        if (source.pid == targetPid)
        {
            capturedExecutable = source.executable;
            break;
        }

    // Match the capture rate to the output device so Windows resamples the source
    // and our FIFO only absorbs clock drift.
    captureStartedRate = currentSampleRate();
    capture.start (targetPid, captureStartedRate);

    useCaptureInput = true;
    rebuildConnections();
    scheduleSave();
    sendChangeMessage();
}

void AudioEngine::setDeviceInput()
{
    capture.stop();

    useCaptureInput = false;
    capturedExecutable.clear();
    rebuildConnections();
    scheduleSave();
    sendChangeMessage();
}

//==============================================================================
juce::File AudioEngine::redirectMarkerFile() const
{
    return getSessionFile().getSiblingFile ("redirect.marker");
}

juce::String AudioEngine::setRedirectedApp (juce::uint32 pid, const juce::String& exe)
{
    if (! AppRouting::isSupported())
        return "Per-app audio routing isn't available on this version of Windows. "
               "Use VIRTUAL CABLE to route your app manually.";

    AppRouting::CablePair cable;
    if (! AppRouting::findCable (cable))
        return "No virtual cable was found. Open VIRTUAL CABLE to install one, then try again.";

    // Record the recovery intent BEFORE applying the route: if we're force-killed in
    // the tiny window between routing the app and persisting the session, the marker
    // still lets the next launch undo it (otherwise the app is left silently playing
    // into the cable). Delete it again if the route itself fails.
    redirectMarkerFile().replaceWithText (exe);

    if (! AppRouting::routeAppOutput (pid, cable.renderDeviceId))
    {
        redirectMarkerFile().deleteFile();
        return "Couldn't redirect that app's audio output. Please try again.";
    }

    applyRedirect (pid, exe, cable.captureName);
    return {};
}

bool AudioEngine::isRedirectedAppRunning() const
{
    if (redirectedApp.isEmpty())
        return true;   // not redirecting — nothing to detect

    return AppRouting::isProcessRunning (redirectedPid);
}

void AudioEngine::applyRedirect (juce::uint32 pid, const juce::String& exe,
                                 const juce::String& captureName)
{
    // Release a previously-redirected app (if switching targets) before taking over.
    if (redirectedApp.isNotEmpty() && redirectedPid != pid)
        AppRouting::clearAppOutput (redirectedPid);

    redirectedApp = exe;
    redirectedPid = pid;

    // Persist that we hold a redirect, so a crash/force-kill can be undone next launch
    // (otherwise the app is left silently playing into the cable).
    redirectMarkerFile().replaceWithText (exe);

    // The app now plays into the cable; read the cable's recording endpoint as our
    // input. This is the ordinary device-input path — no loopback, no muting.
    useCaptureInput = false;

    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.inputDeviceName != captureName)
    {
        setup.inputDeviceName = captureName;
        setup.useDefaultInputChannels = true;
        deviceManager.setAudioDeviceSetup (setup, true);   // broadcasts a device change
    }

    rebuildConnections();
    scheduleSave();
    sendChangeMessage();
}

void AudioEngine::clearRedirectedApp()
{
    if (redirectedApp.isEmpty())
        return;

    AppRouting::clearAppOutput (redirectedPid);
    redirectMarkerFile().deleteFile();
    redirectedApp.clear();
    redirectedPid = 0;

    scheduleSave();
    sendChangeMessage();
}

void AudioEngine::cleanupStaleRedirect()
{
    auto marker = redirectMarkerFile();
    if (! marker.existsAsFile())
        return;

    const auto exe = marker.loadFileAsString().trim();
    marker.deleteFile();

    if (exe.isEmpty())
        return;

    // A previous run (possibly crashed) left this app routed into the cable. Restore
    // it to its normal output. loadSession re-applies the redirect afterwards if the
    // saved session still wants it (so a clean relaunch just re-wires the cable).
    //
    // Clear precisely by executable while the app is running (the override is keyed to
    // the app identity, so a fresh PID of the same exe clears it). If the app isn't
    // running now we have no PID to target, so fall back to clearing ALL persisted
    // per-app overrides — the recovery hammer that guarantees it isn't left silently
    // routed into the cable the next time it launches.
    if (! AppRouting::clearAppOutputByName (exe))
        AppRouting::clearAllOverrides();
}

void AudioEngine::restoreRedirectFromSession (const juce::XmlElement& session)
{
    auto* input = session.getChildByName ("INPUT");
    if (input == nullptr)
        return;

    // "redirect" is the current mode; "capture" is the legacy loopback mode, which we
    // migrate to a redirect (the app-input feature the user now expects).
    const auto mode = input->getStringAttribute ("mode");
    if (mode != "redirect" && mode != "capture")
        return;

    const auto exe = input->getStringAttribute ("exe");
    if (exe.isEmpty())
        return;

    // Re-resolve the saved executable to a running PID (prefer an active session). If
    // it isn't running this launch, stay on the saved input device; the user re-picks.
    juce::uint32 chosen = 0;
    for (const auto& source : enumerateAudioSources())
        if (source.executable.equalsIgnoreCase (exe))
        {
            chosen = source.pid;
            if (source.active)
                break;
        }

    if (chosen != 0)
        setRedirectedApp (chosen, exe);
}

void AudioEngine::restoreInputSource (const juce::XmlElement& session)
{
    auto* input = session.getChildByName ("INPUT");
    if (input == nullptr || input->getStringAttribute ("mode") != "capture")
        return;   // device input is the default — nothing to restore

    const auto exe = input->getStringAttribute ("exe");
    if (exe.isEmpty())
        return;

    // Re-resolve the saved executable to a running PID (prefer an active session).
    // If the app isn't running this launch, stay on device input; the user re-picks.
    juce::uint32 chosen = 0;
    for (const auto& source : enumerateAudioSources())
        if (source.executable.equalsIgnoreCase (exe))
        {
            chosen = source.pid;
            if (source.active)
                break;
        }

    if (chosen != 0)
        setCaptureSource (chosen);
}

//==============================================================================
void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Device changed: channel counts of the graph's IO nodes may have changed,
    // so re-add the connections, and persist the new device setup.
    rebuildConnections();

    // If we're capturing and the output device's rate changed, re-open the capture
    // at the new rate so the FIFO stays 1:1 (no per-block resampling on our side).
    // Skip a restart for a buffer-size-only change — it would needlessly bounce the
    // endpoint mute.
    if (useCaptureInput && capture.isActive())
    {
        const auto rate = currentSampleRate();
        if (std::abs (rate - captureStartedRate) > 1.0)
        {
            capture.start (capture.targetPid(), rate);   // start() stops first; re-mutes
            captureStartedRate = rate;
        }
    }

    scheduleSave();
    sendChangeMessage();
}

void AudioEngine::scheduleSave()
{
    startTimer (saveDebounceMs);
}

void AudioEngine::timerCallback()
{
    saveSession();
    startTimer (autosaveIntervalMs);
}

void AudioEngine::connectNodes (Graph::NodeID source, Graph::NodeID dest)
{
    auto* sourceNode = graph.getNodeForId (source);
    auto* destNode   = graph.getNodeForId (dest);

    if (sourceNode == nullptr || destNode == nullptr)
        return;

    auto numOuts = sourceNode->getProcessor()->getTotalNumOutputChannels();
    auto numIns  = destNode->getProcessor()->getTotalNumInputChannels();

    if (numOuts <= 0 || numIns <= 0)
        return;

    for (int ch = 0; ch < 2; ++ch)
        graph.addConnection ({ { source, juce::jmin (ch, numOuts - 1) },
                               { dest,   juce::jmin (ch, numIns  - 1) } });
}

void AudioEngine::rebuildConnections()
{
    for (auto& connection : graph.getConnections())
        graph.removeConnection (connection);

    // Plain serial chain; the master (limiter) node sits just before the output.
    // Each plugin wrapper handles its own clickless, latency-aligned bypass.
    // The head is either the device input node or the driverless capture source.
    auto previous = useCaptureInput ? captureSourceNodeID : inputNodeID;

    for (auto& slot : slots)
    {
        connectNodes (previous, slot.nodeID);
        previous = slot.nodeID;
    }

    connectNodes (previous, masterNodeID);
    connectNodes (masterNodeID, outputNodeID);
}

//==============================================================================
double AudioEngine::currentSampleRate() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getCurrentSampleRate();

    return 44100.0;
}

int AudioEngine::currentBlockSize() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getCurrentBufferSizeSamples();

    return 512;
}

juce::File AudioEngine::getSessionFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("PluginPlay")
             .getChildFile ("session.xml");
}

//==============================================================================
std::unique_ptr<juce::XmlElement> AudioEngine::createSlotXml (int index) const
{
    auto slotXml = std::make_unique<juce::XmlElement> ("SLOT");
    slotXml->setAttribute ("bypassed", slots[(size_t) index].bypassed);

    if (auto descriptionXml = slots[(size_t) index].description.createXml())
        slotXml->addChildElement (descriptionXml.release());

    if (auto* processor = getProcessor (index))
    {
        juce::MemoryBlock state;
        processor->getStateInformation (state);
        slotXml->setAttribute ("state", state.toBase64Encoding());
    }

    return slotXml;
}

std::unique_ptr<juce::XmlElement> AudioEngine::createChainXml (bool includeGhosts) const
{
    auto chain = std::make_unique<juce::XmlElement> ("CHAIN");

    // Fast path (the normal case): no retained failures, so just serialize the live
    // chain exactly as before. Presets also take this path (includeGhosts = false) so
    // a saved preset never carries a plugin that isn't actually in the chain.
    if (! includeGhosts || ghostSlots.empty())
    {
        for (size_t i = 0; i < slots.size(); ++i)
            chain->addChildElement (createSlotXml ((int) i).release());

        return chain;
    }

    // Merge live slots with retained ghost slots back into the restored chain's original
    // order: each ghost sits at its recorded original index; the live slots (still in
    // original order, minus the ghosts) fill the remaining positions. This re-emits a
    // transiently-missing plugin verbatim instead of overwriting it out of the session.
    const int total = (int) slots.size() + (int) ghostSlots.size();
    size_t liveIdx = 0;

    for (int pos = 0; pos < total; ++pos)
    {
        const juce::XmlElement* ghost = nullptr;
        for (const auto& g : ghostSlots)
            if (g.originalIndex == pos) { ghost = g.xml.get(); break; }

        if (ghost != nullptr)
            chain->addChildElement (new juce::XmlElement (*ghost));
        else if (liveIdx < slots.size())
            chain->addChildElement (createSlotXml ((int) liveIdx++).release());
    }

    // Safety net: append any live slot not placed above (shouldn't happen while the
    // no-structural-edit invariant holds, but never drop a real plugin).
    while (liveIdx < slots.size())
        chain->addChildElement (createSlotXml ((int) liveIdx++).release());

    return chain;
}

void AudioEngine::saveSession()
{
    // Never save before loadSession has run (startup defers it until after the first
    // paint) — saving then would overwrite the on-disk session with an empty one.
    if (restoringSession || ! sessionLoaded)
        return;

    juce::XmlElement session ("SESSION");
    session.setAttribute ("version", 1);
    session.setAttribute ("limiter", limiterEnabled);

    auto* devices = session.createNewChildElement ("DEVICES");
    if (auto deviceState = deviceManager.createStateXml())
        devices->addChildElement (deviceState.release());

    // Input routing. An app-to-cable redirect is saved by executable name (a saved
    // PID would be meaningless next launch); a plain device input saves as "device".
    // The quarantined loopback path only writes "capture" when explicitly enabled.
    auto* input = session.createNewChildElement ("INPUT");
    bool wroteInputMode = false;

    if (redirectedApp.isNotEmpty())
    {
        input->setAttribute ("mode", "redirect");
        input->setAttribute ("exe", redirectedApp);
        wroteInputMode = true;
    }

    if constexpr (enableLoopbackCapture)   // quarantined loopback path
    {
        if (! wroteInputMode && useCaptureInput && capturedExecutable.isNotEmpty())
        {
            input->setAttribute ("mode", "capture");
            input->setAttribute ("exe", capturedExecutable);
            wroteInputMode = true;
        }
    }

    if (! wroteInputMode)
        input->setAttribute ("mode", "device");

    session.addChildElement (createChainXml().release());

    auto file = getSessionFile();
    file.getParentDirectory().createDirectory();

    // Rotate the last known-good session to a backup before overwriting, so a
    // corrupt/truncated primary (older builds, disk trouble) can be rolled back on
    // load rather than silently resetting the user's whole setup.
    if (file.existsAsFile() && juce::XmlDocument::parse (file) != nullptr)
        file.copyFileTo (file.getSiblingFile ("session.bak"));

    session.writeTo (file);
}

//==============================================================================
juce::File AudioEngine::getPresetsDirectory() const
{
    return getSessionFile().getParentDirectory().getChildFile ("Presets");
}

bool AudioEngine::savePreset (const juce::File& presetFile)
{
    presetFile.getParentDirectory().createDirectory();
    // A preset captures the live chain only — never a retained (missing) ghost slot.
    return createChainXml (false)->writeTo (presetFile);
}

bool AudioEngine::loadPreset (const juce::File& presetFile)
{
    auto xml = juce::XmlDocument::parse (presetFile);

    if (xml == nullptr || ! xml->hasTagName ("CHAIN"))
        return false;

    clearChain();   // bumps restoreGeneration

    if (xml->getNumChildElements() > 0)
    {
        restoringSession = true;
        restoreChainFromXml (std::shared_ptr<juce::XmlElement> (xml.release()), 0,
                             std::make_shared<juce::StringArray>(), restoreGeneration);
    }

    return true;
}

void AudioEngine::loadSession()
{
    sessionLoaded = true;  // saves (incl. the shutdown save) are meaningful from here on
    ++restoreGeneration;   // supersede any restore already in flight
    ghostSlots.clear();    // a fresh load starts with no retained failures

    // Undo any app→cable redirect a previous (possibly crashed) run left behind before
    // we re-open devices and re-apply the saved input.
    cleanupStaleRedirect();

    std::unique_ptr<juce::XmlElement> session;

    if (auto file = getSessionFile(); file.existsAsFile())
    {
        session = juce::XmlDocument::parse (file);

        // Primary is corrupt/unparseable: fall back to the last-good backup rather
        // than silently resetting devices + chain to defaults.
        if (session == nullptr)
            session = juce::XmlDocument::parse (file.getSiblingFile ("session.bak"));
    }

    const juce::XmlElement* deviceState = nullptr;
    if (session != nullptr)
    {
        limiterEnabled = session->getBoolAttribute ("limiter", true);
        if (master != nullptr)
            master->setLimiterEnabled (limiterEnabled);

        if (auto* devices = session->getChildByName ("DEVICES"))
            deviceState = devices->getFirstChildElement();
    }

    // No saved device state (first launch): a plain initialise() would open the
    // default input — the microphone — feeding live mic through the speakers. Hand
    // it a state naming only the default output instead, so no input opens (the mic
    // is never touched) until the user picks a source; the walkthrough points them
    // at the INPUT dropdown. initialise() stores this as the chosen setup, so the
    // no-input choice also persists until they do.
    std::unique_ptr<juce::XmlElement> firstRunState;

    if (deviceState == nullptr)
    {
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            const auto outputs = type->getDeviceNames (false);
            if (outputs.isEmpty())
                continue;

            firstRunState = std::make_unique<juce::XmlElement> ("DEVICESETUP");
            firstRunState->setAttribute ("deviceType", type->getTypeName());
            firstRunState->setAttribute ("audioOutputDeviceName",
                                         outputs [type->getDefaultDeviceIndex (false)]);
            deviceState = firstRunState.get();
            break;
        }
    }

    deviceManager.initialise (2, 2, deviceState, true);
    rebuildConnections();
    sendChangeMessage();   // the device is open — selectors and status can populate now

    // The rest of the restore continues in later message-loop turns so the window
    // paints and answers input between the heavy steps. Timer callbacks, not
    // callAsync: posted messages are dispatched BEFORE WM_PAINT, so chaining with
    // callAsync would keep the window frozen; WM_TIMER only fires once paint and
    // input have been serviced.
    if (session != nullptr)
    {
        std::shared_ptr<juce::XmlElement> sharedSession (session.release());
        const int generation = restoreGeneration;

        juce::Timer::callAfterDelay (1,
            [weak = juce::WeakReference<AudioEngine> (this), sharedSession, generation]
            {
                if (weak == nullptr || generation != weak->restoreGeneration)
                    return;

                auto& self = *weak;

                // Restore the input routing now the output device is open. Re-applies an
                // app→cable redirect (migrating legacy loopback "capture" sessions too);
                // does nothing if the saved app isn't running this launch — the user
                // re-picks from the dropdown.
                self.restoreRedirectFromSession (*sharedSession);

                if (auto* chain = sharedSession->getChildByName ("CHAIN"))
                {
                    if (chain->getNumChildElements() > 0)
                    {
                        self.restoringSession = true;
                        auto chainCopy = std::make_shared<juce::XmlElement> (*chain);
                        self.restoreChainFromXml (chainCopy, 0,
                                                  std::make_shared<juce::StringArray>(), generation);
                    }
                }
            });
    }

    // Fill in the drivers we didn't scan at startup (one per turn, after the restore).
    registerRemainingDeviceTypesWhenIdle();
}

void AudioEngine::restoreChainFromXml (std::shared_ptr<juce::XmlElement> chainXml, int slotIndex,
                                       std::shared_ptr<juce::StringArray> failures, int generation)
{
    // A newer load (or a clearChain) has superseded this restore: stop without
    // touching the chain or the saved session.
    if (generation != restoreGeneration)
        return;

    if (slotIndex >= chainXml->getNumChildElements())
    {
        restoringSession = false;
        currentlyRestoring.clear();
        rebuildConnections();

        // Only persist the restored chain if every plugin came back. If any
        // failed to instantiate (a momentarily-locked file, a slow-to-mount
        // drive, a rate-init failure), saving now would overwrite the on-disk
        // session with the pruned chain and delete those plugins for good.
        // Leaving the file intact lets the next launch retry them.
        if (failures == nullptr || failures->isEmpty())
            saveSession();

        sendChangeMessage();

        if (failures != nullptr && ! failures->isEmpty() && onRestoreErrors != nullptr)
            onRestoreErrors (*failures);

        return;
    }

    auto* slotXml = chainXml->getChildElement (slotIndex);
    auto* descriptionXml = slotXml != nullptr ? slotXml->getChildByName ("PLUGIN") : nullptr;

    juce::PluginDescription description;

    if (descriptionXml == nullptr || ! description.loadFromXml (*descriptionXml))
    {
        if (failures != nullptr)
            failures->add (descriptionXml != nullptr
                               ? descriptionXml->getStringAttribute ("name", "Unknown plugin")
                               : "Unknown plugin");

        // Retain the slot verbatim so a save can't delete it — a later launch (or JUCE
        // version) may parse/instantiate it. Keyed by its original chain position.
        if (slotXml != nullptr)
            ghostSlots.push_back ({ slotIndex, std::make_unique<juce::XmlElement> (*slotXml) });

        restoreChainFromXml (chainXml, slotIndex + 1, failures, generation);
        return;
    }

    const auto bypassed = slotXml->getBoolAttribute ("bypassed");
    const auto stateBase64 = slotXml->getStringAttribute ("state");

    // Name the plugin being loaded so the status line can show real progress while
    // the chain comes back (each instantiation blocks the message thread briefly).
    currentlyRestoring = description.name;

    formatManager.createPluginInstanceAsync (description, currentSampleRate(), currentBlockSize(),
        [safeThis = juce::WeakReference<AudioEngine> (this), chainXml, slotIndex, description, bypassed, stateBase64, failures, generation]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String&)
        {
            if (safeThis == nullptr)
                return;

            auto& self = *safeThis;

            // This restore was superseded while the instance was being created
            // (a new load or a clearChain). Drop the plugin instead of appending
            // it to whatever chain is current now.
            if (generation != self.restoreGeneration)
                return;

            if (instance != nullptr)
            {
                instance->enableAllBuses();

                juce::MemoryBlock state;
                if (state.fromBase64Encoding (stateBase64) && state.getSize() > 0)
                    instance->setStateInformation (state.getData(), (int) state.getSize());

                auto wrapper = std::make_unique<PluginNode> (std::move (instance));
                wrapper->setMasterBypass (self.masterBypassed);
                wrapper->setUserBypass (bypassed);
                auto node = self.graph.addNode (std::move (wrapper));
                self.slots.push_back ({ node->nodeID, description, bypassed });

                self.rebuildConnections();
                self.sendChangeMessage();
            }
            else if (failures != nullptr)
            {
                failures->add (description.name);

                // The plugin exists in the known list but couldn't be instantiated right
                // now (locked DLL, unmounted drive, one-off init failure). Keep its slot
                // XML so the next save re-emits it and the next launch retries it, instead
                // of permanently pruning it from the saved chain.
                if (auto* sx = chainXml->getChildElement (slotIndex))
                    self.ghostSlots.push_back ({ slotIndex, std::make_unique<juce::XmlElement> (*sx) });
            }

            // Next slot in a fresh timer turn, not straight away: instantiation runs
            // in a posted message, and posted messages pre-empt WM_PAINT and input,
            // so chaining slots directly keeps the window frozen ("Not Responding")
            // for the whole restore. A timer callback lets paint and clicks through
            // between plugin loads.
            juce::Timer::callAfterDelay (1, [safeThis, chainXml, slotIndex, failures, generation]
            {
                if (safeThis != nullptr)
                    safeThis->restoreChainFromXml (chainXml, slotIndex + 1, failures, generation);
            });
        });
}

void AudioEngine::insertPluginFromXml (std::unique_ptr<juce::XmlElement> slotXml, int index)
{
    if (slotXml == nullptr)
        return;

    auto* descriptionXml = slotXml->getChildByName ("PLUGIN");

    juce::PluginDescription description;
    if (descriptionXml == nullptr || ! description.loadFromXml (*descriptionXml))
        return;

    const auto bypassed    = slotXml->getBoolAttribute ("bypassed");
    const auto stateBase64 = slotXml->getStringAttribute ("state");

    formatManager.createPluginInstanceAsync (description, currentSampleRate(), currentBlockSize(),
        [safeThis = juce::WeakReference<AudioEngine> (this), index, description, bypassed, stateBase64]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String&)
        {
            if (safeThis == nullptr || instance == nullptr)
                return;

            auto& self = *safeThis;

            instance->enableAllBuses();

            juce::MemoryBlock state;
            if (state.fromBase64Encoding (stateBase64) && state.getSize() > 0)
                instance->setStateInformation (state.getData(), (int) state.getSize());

            auto wrapper = std::make_unique<PluginNode> (std::move (instance));
            wrapper->setMasterBypass (self.masterBypassed);
            wrapper->setUserBypass (bypassed);
            auto node = self.graph.addNode (std::move (wrapper));

            const auto at = (size_t) juce::jlimit (0, (int) self.slots.size(), index);
            self.slots.insert (self.slots.begin() + (long) at, { node->nodeID, description, bypassed });

            self.rebuildConnections();
            self.scheduleSave();
            self.sendChangeMessage();
        });
}

} // namespace play

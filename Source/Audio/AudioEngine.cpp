#include "AudioEngine.h"

namespace play
{

using Graph  = juce::AudioProcessorGraph;
using IONode = juce::AudioProcessorGraph::AudioGraphIOProcessor;

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

    auto inNode  = graph.addNode (std::make_unique<IONode> (IONode::audioInputNode));
    auto outNode = graph.addNode (std::make_unique<IONode> (IONode::audioOutputNode));
    inputNodeID  = inNode->nodeID;
    outputNodeID = outNode->nodeID;

    player.setProcessor (&graph);
    deviceManager.addAudioCallback (&meterTap);
    deviceManager.addChangeListener (this);

    startTimer (autosaveIntervalMs);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    saveSession();

    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&meterTap);
    player.setProcessor (nullptr);
    graph.clear();
}

//==============================================================================
juce::AudioProcessor* AudioEngine::getProcessor (int index) const
{
    if (auto* node = graph.getNodeForId (slots[(size_t) index].nodeID))
        return node->getProcessor();

    return nullptr;
}

void AudioEngine::addPlugin (const juce::PluginDescription& description,
                             std::function<void (const juce::String&)> onDone)
{
    formatManager.createPluginInstanceAsync (description, currentSampleRate(), currentBlockSize(),
        [this, description, onDone] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (instance == nullptr)
            {
                if (onDone != nullptr)
                    onDone (error);
                return;
            }

            instance->enableAllBuses();
            auto node = graph.addNode (std::move (instance));
            node->setBypassed (masterBypassed);
            slots.push_back ({ node->nodeID, description, false });

            rebuildConnections();
            scheduleSave();
            sendChangeMessage();

            if (onDone != nullptr)
                onDone ({});
        });
}

void AudioEngine::removePlugin (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) slots.size()))
        return;

    auto nodeID = slots[(size_t) index].nodeID;

    if (onPluginAboutToBeRemoved != nullptr)
        onPluginAboutToBeRemoved (nodeID);

    slots.erase (slots.begin() + index);
    graph.removeNode (nodeID);

    rebuildConnections();
    scheduleSave();
    sendChangeMessage();
}

void AudioEngine::clearChain()
{
    while (! slots.empty())
        removePlugin ((int) slots.size() - 1);
}

void AudioEngine::movePlugin (int fromIndex, int toIndex)
{
    toIndex = juce::jlimit (0, (int) slots.size() - 1, toIndex);

    if (! juce::isPositiveAndBelow (fromIndex, (int) slots.size()) || fromIndex == toIndex)
        return;

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

    if (auto* node = graph.getNodeForId (slots[(size_t) index].nodeID))
        node->setBypassed (masterBypassed || shouldBeBypassed);

    scheduleSave();
    sendChangeMessage();
}

void AudioEngine::setMasterBypass (bool shouldBypass)
{
    if (masterBypassed == shouldBypass)
        return;

    masterBypassed = shouldBypass;

    for (auto& slot : slots)
        if (auto* node = graph.getNodeForId (slot.nodeID))
            node->setBypassed (masterBypassed || slot.bypassed);

    sendChangeMessage();
}

//==============================================================================
void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Device changed: channel counts of the graph's IO nodes may have changed,
    // so re-add the connections, and persist the new device setup.
    rebuildConnections();
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

    auto previous = inputNodeID;

    for (auto& slot : slots)
    {
        connectNodes (previous, slot.nodeID);
        previous = slot.nodeID;
    }

    connectNodes (previous, outputNodeID);
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
std::unique_ptr<juce::XmlElement> AudioEngine::createChainXml() const
{
    auto chain = std::make_unique<juce::XmlElement> ("CHAIN");

    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto* slotXml = chain->createNewChildElement ("SLOT");
        slotXml->setAttribute ("bypassed", slots[i].bypassed);

        if (auto descriptionXml = slots[i].description.createXml())
            slotXml->addChildElement (descriptionXml.release());

        if (auto* processor = getProcessor ((int) i))
        {
            juce::MemoryBlock state;
            processor->getStateInformation (state);
            slotXml->setAttribute ("state", state.toBase64Encoding());
        }
    }

    return chain;
}

void AudioEngine::saveSession()
{
    if (restoringSession)
        return;

    juce::XmlElement session ("SESSION");

    auto* devices = session.createNewChildElement ("DEVICES");
    if (auto deviceState = deviceManager.createStateXml())
        devices->addChildElement (deviceState.release());

    session.addChildElement (createChainXml().release());

    auto file = getSessionFile();
    file.getParentDirectory().createDirectory();
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
    return createChainXml()->writeTo (presetFile);
}

bool AudioEngine::loadPreset (const juce::File& presetFile)
{
    auto xml = juce::XmlDocument::parse (presetFile);

    if (xml == nullptr || ! xml->hasTagName ("CHAIN"))
        return false;

    clearChain();

    if (xml->getNumChildElements() > 0)
    {
        restoringSession = true;
        restoreChainFromXml (std::shared_ptr<juce::XmlElement> (xml.release()), 0);
    }

    return true;
}

void AudioEngine::loadSession()
{
    std::unique_ptr<juce::XmlElement> session;

    if (auto file = getSessionFile(); file.existsAsFile())
        session = juce::XmlDocument::parse (file);

    const juce::XmlElement* deviceState = nullptr;
    if (session != nullptr)
        if (auto* devices = session->getChildByName ("DEVICES"))
            deviceState = devices->getFirstChildElement();

    deviceManager.initialise (2, 2, deviceState, true);
    rebuildConnections();

    if (session != nullptr)
    {
        if (auto* chain = session->getChildByName ("CHAIN"))
        {
            if (chain->getNumChildElements() > 0)
            {
                restoringSession = true;
                auto chainCopy = std::make_shared<juce::XmlElement> (*chain);
                restoreChainFromXml (chainCopy, 0);
                return;
            }
        }
    }

    sendChangeMessage();
}

void AudioEngine::restoreChainFromXml (std::shared_ptr<juce::XmlElement> chainXml, int slotIndex)
{
    if (slotIndex >= chainXml->getNumChildElements())
    {
        restoringSession = false;
        rebuildConnections();
        saveSession();
        sendChangeMessage();
        return;
    }

    auto* slotXml = chainXml->getChildElement (slotIndex);
    auto* descriptionXml = slotXml != nullptr ? slotXml->getChildByName ("PLUGIN") : nullptr;

    juce::PluginDescription description;

    if (descriptionXml == nullptr || ! description.loadFromXml (*descriptionXml))
    {
        restoreChainFromXml (chainXml, slotIndex + 1);
        return;
    }

    const auto bypassed = slotXml->getBoolAttribute ("bypassed");
    const auto stateBase64 = slotXml->getStringAttribute ("state");

    formatManager.createPluginInstanceAsync (description, currentSampleRate(), currentBlockSize(),
        [this, chainXml, slotIndex, description, bypassed, stateBase64]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String&)
        {
            if (instance != nullptr)
            {
                instance->enableAllBuses();

                juce::MemoryBlock state;
                if (state.fromBase64Encoding (stateBase64) && state.getSize() > 0)
                    instance->setStateInformation (state.getData(), (int) state.getSize());

                auto node = graph.addNode (std::move (instance));
                node->setBypassed (masterBypassed || bypassed);
                slots.push_back ({ node->nodeID, description, bypassed });

                rebuildConnections();
                sendChangeMessage();
            }

            restoreChainFromXml (chainXml, slotIndex + 1);
        });
}

} // namespace play

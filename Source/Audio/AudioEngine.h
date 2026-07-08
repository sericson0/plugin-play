#pragma once

#include <JuceHeader.h>
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
    /** Input routing. The chain is fed either by the selected input DEVICE
        (default; virtual-cable / hardware-loopback path) or by DRIVERLESS
        process-loopback CAPTURE of a chosen application, which also master-mutes
        the default render endpoint to kill the app's dry signal at the speakers. */
    std::vector<AudioSource> availableCaptureSources() const { return enumerateAudioSources(); }

    /** Switch the chain's input to driverless capture of the given process. */
    void setCaptureSource (juce::uint32 pid);

    /** Switch the chain's input back to the selected audio input device. */
    void setDeviceInput();

    bool isCapturingInput() const noexcept          { return useCaptureInput; }
    juce::uint32 capturedSourcePid() const noexcept { return capture.targetPid(); }

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
    /** Restores devices + chain from disk (or opens default devices on first run). */
    void loadSession();
    void saveSession();

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
    std::unique_ptr<juce::XmlElement> createChainXml() const;

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

    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    MeterTap meterTap { player, *this };

    juce::AudioProcessorGraph::NodeID inputNodeID, outputNodeID, masterNodeID;
    juce::AudioProcessorGraph::NodeID captureSourceNodeID;
    MasterProcessor* master = nullptr;
    std::vector<PluginSlotInfo> slots;

    // Driverless process-loopback capture. When useCaptureInput is true the chain
    // head is captureSourceNode (fed by `capture`) instead of the device input.
    LoopbackCapture capture;
    bool useCaptureInput = false;
    double captureStartedRate = 0.0;

    std::atomic<float> inputPeaks[2]  { 0.0f, 0.0f };
    std::atomic<float> outputPeaks[2] { 0.0f, 0.0f };

    bool restoringSession = false;
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

    JUCE_DECLARE_WEAK_REFERENCEABLE (AudioEngine)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace play

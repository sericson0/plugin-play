#pragma once

#include <JuceHeader.h>

namespace play
{

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
                    private juce::ChangeListener
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

    /** Called just before a plugin node is destroyed, so the UI can close its window. */
    std::function<void (juce::AudioProcessorGraph::NodeID)> onPluginAboutToBeRemoved;

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

    void rebuildConnections();
    void connectNodes (juce::AudioProcessorGraph::NodeID source, juce::AudioProcessorGraph::NodeID dest);
    void restoreChainFromXml (std::shared_ptr<juce::XmlElement> chainXml, int slotIndex);

    juce::File getSessionFile() const;
    double currentSampleRate() const;
    int currentBlockSize() const;

    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    MeterTap meterTap { player, *this };

    juce::AudioProcessorGraph::NodeID inputNodeID, outputNodeID;
    std::vector<PluginSlotInfo> slots;

    std::atomic<float> inputPeaks[2]  { 0.0f, 0.0f };
    std::atomic<float> outputPeaks[2] { 0.0f, 0.0f };

    bool restoringSession = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace play

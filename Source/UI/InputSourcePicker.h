#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"

namespace play
{

//==============================================================================
/** Driverless-capture source picker, shown as a dialog from the header.

    Lets the DJ choose between the selected audio INPUT DEVICE (virtual-cable /
    hardware-loopback path) and DRIVERLESS CAPTURE of a running application
    (process loopback, no install). Picking an app hands its PID to
    AudioEngine::setCaptureSource, which starts capture and master-mutes the
    default render endpoint to kill the app's dry signal at the speakers. */
class InputSourcePicker : public juce::Component
{
public:
    explicit InputSourcePicker (AudioEngine&);

    /** Opens the picker as an async dialog styled like the rest of the app. */
    static void launch (AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refresh();
    void apply();
    void updateStatus();

    AudioEngine& engine;

    juce::Label      heading;
    juce::Label      statusLabel;
    juce::TextEditor body;
    juce::ComboBox   sourceBox;
    juce::TextButton applyButton   { "USE THIS SOURCE" };
    juce::TextButton refreshButton { "REFRESH" };
    juce::TextButton closeButton   { "CLOSE" };

    // Combo item ids: 1 = audio input device; 2..N+1 = sources[id - 2].
    static constexpr int deviceItemId = 1;
    std::vector<AudioSource> sources;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputSourcePicker)
};

} // namespace play

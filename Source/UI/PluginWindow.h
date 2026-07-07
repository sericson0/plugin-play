#pragma once

#include <JuceHeader.h>
#include "../Theme.h"

namespace play
{

//==============================================================================
/** A top-level window showing a hosted plugin's own editor (or a generic
    parameter panel if the plugin has no GUI). */
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessor& processor,
                  const juce::String& title,
                  std::function<void()> onCloseRequest)
        : juce::DocumentWindow (title, play::Colours::background,
                                juce::DocumentWindow::closeButton),
          onClose (std::move (onCloseRequest))
    {
        setUsingNativeTitleBar (true);

        if (auto* editor = processor.createEditorIfNeeded())
        {
            setResizable (editor->isResizable(), false);
            setContentOwned (editor, true);
        }
        else
        {
            setResizable (true, false);
            setContentOwned (new juce::GenericAudioProcessorEditor (processor), true);
            centreWithSize (420, 500);
        }

        centreAroundComponent (nullptr, getWidth(), getHeight());
        setVisible (true);
        toFront (true);
        play::applyDarkTitleBar (*this);
    }

    void closeButtonPressed() override
    {
        if (onClose != nullptr)
            onClose();   // owner destroys this window
    }

private:
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

} // namespace play

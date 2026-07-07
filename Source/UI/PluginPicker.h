#pragma once

#include <JuceHeader.h>

namespace play
{

//==============================================================================
/**
    Type-to-search plugin list, shown in a CallOutBox from the "+ Add Plugin"
    button. The search box filters by plugin name or manufacturer; Up/Down
    move the selection, Return (or a click) picks, Escape dismisses.
*/
class PluginPicker : public juce::Component,
                     private juce::ListBoxModel
{
public:
    PluginPicker (const juce::Array<juce::PluginDescription>& availableTypes,
                  std::function<void (const juce::PluginDescription&)> onPickCallback);

    void resized() override;
    void parentHierarchyChanged() override;

private:
    //==============================================================================
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;

    void applyFilter();
    void pickRow (int row);
    bool handleNavigationKey (const juce::KeyPress&);
    void dismiss();

    //==============================================================================
    /** TextEditor that hands list-navigation keys to the picker before using them. */
    struct SearchEditor : public juce::TextEditor
    {
        std::function<bool (const juce::KeyPress&)> onNavigationKey;

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (onNavigationKey != nullptr && onNavigationKey (key))
                return true;

            return juce::TextEditor::keyPressed (key);
        }
    };

    juce::Array<juce::PluginDescription> types;   // sorted by name
    juce::Array<int> filtered;                    // indexes into types
    std::function<void (const juce::PluginDescription&)> onPick;

    SearchEditor searchBox;
    juce::ListBox list;
    juce::Label noMatchesLabel { {}, "No matching plugins" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginPicker)
};

} // namespace play

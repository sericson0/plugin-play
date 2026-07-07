#include "PluginPicker.h"
#include "../Theme.h"

namespace play
{

using namespace play::Colours;

//==============================================================================
PluginPicker::PluginPicker (const juce::Array<juce::PluginDescription>& availableTypes,
                            std::function<void (const juce::PluginDescription&)> onPickCallback)
    : types (availableTypes), onPick (std::move (onPickCallback))
{
    std::sort (types.begin(), types.end(),
               [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
               {
                   return a.name.compareIgnoreCase (b.name) < 0;
               });

    searchBox.setTextToShowWhenEmpty ("Search plugins...", gridText);
    searchBox.setFont (juce::Font (juce::FontOptions (15.0f)));
    searchBox.setEscapeAndReturnKeysConsumed (true);
    searchBox.onTextChange = [this] { applyFilter(); };
    searchBox.onReturnKey  = [this] { pickRow (list.getSelectedRow()); };
    searchBox.onEscapeKey  = [this] { dismiss(); };
    searchBox.onNavigationKey = [this] (const juce::KeyPress& key) { return handleNavigationKey (key); };
    addAndMakeVisible (searchBox);

    list.setModel (this);
    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);

    noMatchesLabel.setJustificationType (juce::Justification::centred);
    noMatchesLabel.setColour (juce::Label::textColourId, gridText);
    addChildComponent (noMatchesLabel);

    setSize (380, 420);
    applyFilter();
}

void PluginPicker::resized()
{
    auto area = getLocalBounds().reduced (8);
    searchBox.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);
    list.setBounds (area);
    noMatchesLabel.setBounds (area.removeFromTop (60));
}

void PluginPicker::parentHierarchyChanged()
{
    // Focus the search box once the CallOutBox has put us on the desktop.
    if (isShowing())
    {
        juce::Component::SafePointer<PluginPicker> self (this);
        juce::MessageManager::callAsync ([self]
        {
            if (self != nullptr)
                self->searchBox.grabKeyboardFocus();
        });
    }
}

//==============================================================================
int PluginPicker::getNumRows()
{
    return filtered.size();
}

void PluginPicker::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, filtered.size()))
        return;

    const auto& description = types.getReference (filtered[row]);
    auto area = juce::Rectangle<int> (0, 0, width, height);

    if (selected)
    {
        g.setColour (buttonSelected);
        g.fillRoundedRectangle (area.toFloat().reduced (1.0f), 4.0f);
    }

    area.reduce (8, 0);

    g.setColour (gridText);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    const auto manufacturerWidth = juce::jmin (area.getWidth() / 2, 140);
    g.drawText (description.manufacturerName, area.removeFromRight (manufacturerWidth),
                juce::Justification::centredRight, true);

    g.setColour (selected ? textBright : textNormal);
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    g.drawText (description.name, area.withTrimmedRight (8),
                juce::Justification::centredLeft, true);
}

void PluginPicker::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    pickRow (row);
}

void PluginPicker::returnKeyPressed (int row)
{
    pickRow (row);
}

//==============================================================================
void PluginPicker::applyFilter()
{
    const auto needle = searchBox.getText().trim();
    filtered.clearQuick();

    for (int i = 0; i < types.size(); ++i)
    {
        const auto& description = types.getReference (i);

        if (needle.isEmpty()
             || description.name.containsIgnoreCase (needle)
             || description.manufacturerName.containsIgnoreCase (needle))
            filtered.add (i);
    }

    noMatchesLabel.setVisible (filtered.isEmpty());
    list.updateContent();

    if (! filtered.isEmpty())
        list.selectRow (0);
}

void PluginPicker::pickRow (int row)
{
    if (! juce::isPositiveAndBelow (row, filtered.size()))
        return;

    const auto description = types.getReference (filtered[row]);
    dismiss();

    if (onPick != nullptr)
        onPick (description);
}

bool PluginPicker::handleNavigationKey (const juce::KeyPress& key)
{
    auto moveSelection = [this] (int delta)
    {
        if (filtered.isEmpty())
            return;

        const auto row = juce::jlimit (0, filtered.size() - 1, list.getSelectedRow() + delta);
        list.selectRow (row);
    };

    if (key == juce::KeyPress::upKey)         { moveSelection (-1); return true; }
    if (key == juce::KeyPress::downKey)       { moveSelection (1);  return true; }
    if (key == juce::KeyPress::pageUpKey)     { moveSelection (-8); return true; }
    if (key == juce::KeyPress::pageDownKey)   { moveSelection (8);  return true; }

    return false;
}

void PluginPicker::dismiss()
{
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

} // namespace play

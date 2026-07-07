#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"

namespace play
{

//==============================================================================
/**
    The vertical list of plugin slot cards plus the "+ Add Plugin" button.
    Cards can be dragged to reorder; each has bypass / edit / remove controls.
*/
class ChainView : public juce::Component
{
public:
    explicit ChainView (AudioEngine&);
    ~ChainView() override;

    /** Syncs the slot cards with the engine's current chain, reusing
        existing cards so unrelated state (hover, focus) survives. */
    void refresh();

    /** Height needed to show all cards; the parent viewport uses this. */
    int getIdealHeight() const;

    std::function<void (juce::Point<int> screenPosition)> onAddClicked;
    std::function<void (int slotIndex)> onOpenEditor;

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int cardHeight = 58;
    static constexpr int cardGap    = 8;

private:
    class SlotCard;

    void layoutCards (bool animateOthers);
    int indexOfCard (SlotCard*) const;

    // drag-reorder state
    void cardDragStarted (SlotCard*);
    void cardDragMoved (SlotCard*, int distanceY);
    void cardDragEnded (SlotCard*);

    AudioEngine& engine;
    juce::OwnedArray<SlotCard> cards;
    juce::TextButton addButton { "+  Add Plugin" };
    juce::ComponentAnimator animator;

    SlotCard* draggedCard = nullptr;
    int draggedStartY = 0;
    int dropTargetIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainView)
};

} // namespace play

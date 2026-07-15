#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"

namespace play
{

//==============================================================================
/**
    The vertical list of plugin slot cards. Cards can be dragged to reorder;
    each has bypass / open / float / remove controls. The "Add Plugin" action
    lives in the toolbar above this view (owned by MainComponent).
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

    std::function<void (int slotIndex)> onOpenEditor;

    /** Toggle whether the plugin's editor is pinned always-on-top, so it stays
        visible over other apps (e.g. the user's DJ software). This never opens
        the editor — it only affects a window that is already (or later) open. */
    std::function<void (int slotIndex, bool shouldFloat)> onFloatEditor;

    /** Queried when a card refreshes so its FLOAT toggle reflects the real state. */
    std::function<bool (int slotIndex)> isFloating;

    /** Queried when a card refreshes so its OPEN button lights up while that
        plugin's editor window is actually open. */
    std::function<bool (int slotIndex)> isEditorOpen;

    /** Invoked when the user clicks the empty-state placeholder ("add your first
        plugin"). Wired by MainComponent to the same menu as the toolbar button. */
    std::function<void()> onAddPlugin;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

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
    juce::ComponentAnimator animator;

    SlotCard* draggedCard = nullptr;
    int draggedStartY = 0;
    int dropTargetIndex = -1;

    // Clickable placeholder shown when the chain is empty (see paint()).
    juce::Rectangle<int> emptyStateBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainView)
};

} // namespace play

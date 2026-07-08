#include "ChainView.h"
#include "../Theme.h"

namespace play
{

using namespace play::Colours;

//==============================================================================
class ChainView::SlotCard : public juce::Component,
                            public juce::SettableTooltipClient
{
public:
    SlotCard (ChainView& ownerView, int slotIndex)
        : owner (ownerView), index (slotIndex)
    {
        bypassButton.setClickingTogglesState (false);
        bypassButton.onClick = [this]
        {
            auto nowBypassed = ! owner.engine.getSlot (index).bypassed;
            owner.engine.setBypassed (index, nowBypassed);
        };

        openButton.onClick = [this]
        {
            if (owner.onOpenEditor != nullptr)
                owner.onOpenEditor (index);
        };

        floatButton.setClickingTogglesState (true);
        floatButton.setColour (juce::TextButton::buttonOnColourId, buttonSelected);
        floatButton.onClick = [this]
        {
            if (owner.onFloatEditor != nullptr)
                owner.onFloatEditor (index, floatButton.getToggleState());
        };

        removeButton.onClick = [this] { owner.engine.removePlugin (index); };

        bypassButton.setTooltip ("Toggle this effect on or off (bypass)");
        openButton  .setTooltip ("Open this plugin's editor window");
        floatButton .setTooltip ("Pin this plugin's editor on top of other windows — "
                                 "keep it visible over your DJ software. Does not open the editor.");
        removeButton.setTooltip ("Remove this plugin from the chain");
        setTooltip ("Drag to reorder — double-click to open the editor");

        addAndMakeVisible (bypassButton);
        addAndMakeVisible (openButton);
        addAndMakeVisible (floatButton);
        addAndMakeVisible (removeButton);

        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        update (slotIndex);
    }

    /** Re-binds this card to the slot at the given index and syncs its controls. */
    void update (int newIndex)
    {
        index = newIndex;
        const auto& slot = owner.engine.getSlot (index);

        bypassButton.setToggleState (! slot.bypassed, juce::dontSendNotification);
        bypassButton.setButtonText (slot.bypassed ? "OFF" : "ON");
        bypassButton.setColour (juce::TextButton::textColourOffId,
                                slot.bypassed ? inactive : textNormal);

        if (owner.isFloating != nullptr)
            floatButton.setToggleState (owner.isFloating (index), juce::dontSendNotification);

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const auto& slot = owner.engine.getSlot (index);

        g.setColour (dragging ? buttonBgHover : buttonBg);
        g.fillRoundedRectangle (bounds, 7.0f);

        g.setColour (dragging ? accent : gridLine.brighter (0.3f));
        g.drawRoundedRectangle (bounds, 7.0f, dragging ? 1.5f : 1.0f);

        auto area = getLocalBounds().reduced (12, 8);

        // grip dots
        auto gripArea = area.removeFromLeft (14);
        g.setColour (gridText);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                g.fillEllipse ((float) gripArea.getX() + (float) col * 6.0f,
                               (float) getHeight() / 2.0f - 8.0f + (float) row * 7.0f,
                               2.5f, 2.5f);

        area.removeFromLeft (8);

        // index badge
        auto badge = area.removeFromLeft (24).withSizeKeepingCentre (22, 22).toFloat();
        g.setColour (slot.bypassed ? sliderTrack : accentDim);
        g.fillRoundedRectangle (badge, 5.0f);
        g.setColour (slot.bypassed ? inactive : textBright);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (juce::String (index + 1), badge, juce::Justification::centred);

        area.removeFromLeft (10);
        area.removeFromRight (buttonStripWidth());

        // plugin name + manufacturer
        g.setColour (slot.bypassed ? inactive : textBright);
        g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        g.drawText (slot.description.name, area.removeFromTop (area.getHeight() / 2),
                    juce::Justification::bottomLeft, true);

        g.setColour (slot.bypassed ? inactive.withAlpha (0.6f) : textNormal);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (slot.description.manufacturerName, area,
                    juce::Justification::topLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 0);
        auto strip = area.removeFromRight (buttonStripWidth());

        removeButton.setBounds (strip.removeFromRight (28).withSizeKeepingCentre (26, 26));
        strip.removeFromRight (6);
        floatButton.setBounds (strip.removeFromRight (54).withSizeKeepingCentre (54, 26));
        strip.removeFromRight (6);
        openButton.setBounds (strip.removeFromRight (54).withSizeKeepingCentre (54, 26));
        strip.removeFromRight (6);
        bypassButton.setBounds (strip.removeFromRight (44).withSizeKeepingCentre (44, 26));
    }

    // A drag only engages after a small movement threshold, so plain clicks
    // and double-clicks don't flash the drag highlight or start a reorder.
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging && e.getDistanceFromDragStart() > 4)
        {
            dragging = true;
            owner.cardDragStarted (this);
            repaint();
        }

        if (dragging)
            owner.cardDragMoved (this, e.getDistanceFromDragStartY());
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
        {
            dragging = false;
            owner.cardDragEnded (this);
            repaint();
        }
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (owner.onOpenEditor != nullptr)
            owner.onOpenEditor (index);
    }

    bool dragging = false;

private:
    static int buttonStripWidth() { return 44 + 6 + 54 + 6 + 54 + 6 + 28; }

    ChainView& owner;
    int index;

    juce::TextButton bypassButton { "ON" };
    juce::TextButton openButton   { "OPEN" };
    juce::TextButton floatButton  { "FLOAT" };
    juce::TextButton removeButton { juce::String::fromUTF8 ("\xc3\x97") }; // ×
};

//==============================================================================
ChainView::ChainView (AudioEngine& engineToUse)
    : engine (engineToUse)
{
    refresh();
}

ChainView::~ChainView() = default;

void ChainView::refresh()
{
    draggedCard = nullptr;
    dropTargetIndex = -1;

    while (cards.size() > engine.getNumPlugins())
        cards.removeLast();

    while (cards.size() < engine.getNumPlugins())
        addAndMakeVisible (cards.add (new SlotCard (*this, cards.size())));

    for (int i = 0; i < cards.size(); ++i)
        cards[i]->update (i);

    setSize (getWidth(), getIdealHeight());
    layoutCards (true);
    repaint();
}

int ChainView::getIdealHeight() const
{
    const int count = engine.getNumPlugins();

    if (count == 0)
        return cardGap + 44 + cardGap;   // room for the empty-state hint

    return count * (cardHeight + cardGap) + cardGap;
}

void ChainView::resized()
{
    layoutCards (false);
}

void ChainView::layoutCards (bool animate)
{
    auto width = juce::jmax (0, getWidth());

    auto place = [this, animate] (juce::Component* comp, juce::Rectangle<int> target)
    {
        // getComponentDestination returns the current bounds when not
        // animating, so this also skips components already in place.
        if (animate && ! comp->getBounds().isEmpty())
        {
            if (animator.getComponentDestination (comp) != target)
                animator.animateComponent (comp, target, 1.0f, 140, false, 1.0, 0.0);
        }
        else
        {
            animator.cancelAnimation (comp, false);
            comp->setBounds (target);
        }
    };

    for (int i = 0; i < cards.size(); ++i)
    {
        // While dragging, leave the gap at the current drop target and let
        // the dragged card float freely.
        int visualIndex = i;

        if (draggedCard != nullptr)
        {
            auto draggedIndex = cards.indexOf (draggedCard);

            if (cards[i] == draggedCard)
                continue;

            visualIndex = i < draggedIndex ? i : i - 1;
            if (visualIndex >= dropTargetIndex)
                ++visualIndex;
        }

        place (cards[i], { 0, cardGap + visualIndex * (cardHeight + cardGap),
                           width, cardHeight });
    }
}

void ChainView::paint (juce::Graphics& g)
{
    if (cards.isEmpty())
    {
        g.setColour (gridText);
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawText ("No plugins in the chain yet",
                    getLocalBounds().removeFromTop (cardGap + 44),
                    juce::Justification::centred);
    }

    // connection line down the left edge suggesting signal flow
    if (cards.size() > 1)
    {
        g.setColour (gridLine.brighter (0.15f));
        g.fillRect (24, cardGap + cardHeight / 2, 2,
                    (cards.size() - 1) * (cardHeight + cardGap));
    }
}

int ChainView::indexOfCard (SlotCard* card) const
{
    return cards.indexOf (card);
}

//==============================================================================
void ChainView::cardDragStarted (SlotCard* card)
{
    draggedCard = card;
    draggedStartY = card->getY();
    dropTargetIndex = indexOfCard (card);
    card->toFront (false);
}

void ChainView::cardDragMoved (SlotCard* card, int distanceY)
{
    if (card != draggedCard)
        return;

    auto newY = juce::jlimit (0, juce::jmax (0, getHeight() - cardHeight),
                              draggedStartY + distanceY);
    card->setTopLeftPosition (card->getX(), newY);

    dropTargetIndex = juce::jlimit (0, cards.size() - 1,
                                    (newY + cardHeight / 2) / (cardHeight + cardGap));
    layoutCards (true);
    repaint();
}

void ChainView::cardDragEnded (SlotCard* card)
{
    if (card != draggedCard)
        return;

    auto from = indexOfCard (card);
    auto to = dropTargetIndex;

    draggedCard = nullptr;
    dropTargetIndex = -1;

    if (from != to && from >= 0)
    {
        // Match the card order to the new chain order first, so the change
        // broadcast's refresh() re-binds cards in place and the dropped card
        // animates from where it was released into its new slot.
        cards.move (from, to);
        engine.movePlugin (from, to);   // triggers a change broadcast -> refresh()
    }
    else
    {
        layoutCards (true);
    }

    repaint();
}

} // namespace play

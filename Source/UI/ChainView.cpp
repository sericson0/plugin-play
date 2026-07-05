#include "ChainView.h"
#include "../Theme.h"

namespace play
{

using namespace play::Colours;

//==============================================================================
class ChainView::SlotCard : public juce::Component
{
public:
    SlotCard (ChainView& ownerView, int slotIndex)
        : owner (ownerView), index (slotIndex)
    {
        const auto& slot = owner.engine.getSlot (index);

        bypassButton.setClickingTogglesState (false);
        bypassButton.setToggleState (! slot.bypassed, juce::dontSendNotification);
        bypassButton.setButtonText (slot.bypassed ? "OFF" : "ON");
        bypassButton.onClick = [this]
        {
            auto nowBypassed = ! owner.engine.getSlot (index).bypassed;
            owner.engine.setBypassed (index, nowBypassed);
        };

        editButton.onClick = [this]
        {
            if (owner.onOpenEditor != nullptr)
                owner.onOpenEditor (index);
        };

        removeButton.onClick = [this] { owner.engine.removePlugin (index); };

        if (slot.bypassed)
            bypassButton.setColour (juce::TextButton::textColourOffId, inactive);

        addAndMakeVisible (bypassButton);
        addAndMakeVisible (editButton);
        addAndMakeVisible (removeButton);

        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
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
        editButton.setBounds (strip.removeFromRight (52).withSizeKeepingCentre (52, 26));
        strip.removeFromRight (6);
        bypassButton.setBounds (strip.removeFromRight (44).withSizeKeepingCentre (44, 26));
    }

    void mouseDown (const juce::MouseEvent&) override   { owner.cardDragStarted (this); dragging = true; repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override { owner.cardDragMoved (this, e.getDistanceFromDragStartY()); }
    void mouseUp (const juce::MouseEvent&) override     { dragging = false; owner.cardDragEnded (this); }

    bool dragging = false;

private:
    static int buttonStripWidth() { return 44 + 6 + 52 + 6 + 28; }

    ChainView& owner;
    int index;

    juce::TextButton bypassButton { "ON" };
    juce::TextButton editButton   { "EDIT" };
    juce::TextButton removeButton { juce::String::fromUTF8 ("\xc3\x97") }; // ×
};

//==============================================================================
ChainView::ChainView (AudioEngine& engineToUse)
    : engine (engineToUse)
{
    addButton.setColour (juce::TextButton::buttonColourId, background.brighter (0.03f));
    addButton.setColour (juce::TextButton::textColourOffId, accentBright);
    addButton.onClick = [this]
    {
        if (onAddClicked != nullptr)
            onAddClicked (addButton.getScreenPosition());
    };
    addAndMakeVisible (addButton);

    refresh();
}

ChainView::~ChainView() = default;

void ChainView::refresh()
{
    draggedCard = nullptr;
    dropTargetIndex = -1;
    cards.clear();

    for (int i = 0; i < engine.getNumPlugins(); ++i)
        addAndMakeVisible (cards.add (new SlotCard (*this, i)));

    setSize (getWidth(), getIdealHeight());
    layoutCards (false);
    repaint();
}

int ChainView::getIdealHeight() const
{
    return (engine.getNumPlugins() + 1) * (cardHeight + cardGap) + cardGap + 40;
}

void ChainView::resized()
{
    layoutCards (false);
}

void ChainView::layoutCards (bool)
{
    auto width = juce::jmax (0, getWidth());
    int y = cardGap;

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

        cards[i]->setBounds (0, cardGap + visualIndex * (cardHeight + cardGap),
                             width, cardHeight);
        y = juce::jmax (y, cards[i]->getBottom() + cardGap);
    }

    if (draggedCard != nullptr)
        y = juce::jmax (y, cardGap + cards.size() * (cardHeight + cardGap));

    if (cards.isEmpty())
        y = cardGap + 44;   // room for the empty-state hint

    addButton.setBounds (0, y, width, cardHeight - 14);
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
        engine.movePlugin (from, to);   // triggers a change broadcast -> refresh()
    else
        layoutCards (false);

    repaint();
}

} // namespace play

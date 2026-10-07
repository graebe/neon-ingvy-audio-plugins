// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A row of sequencer steps that wraps. StepGrid.h has the card and the rules.
 */
#include "StepGrid.h"

#include "ChildLights.h"
#include "Focus.h"
#include "Info.h"
#include "Keys.h"
#include "UvLight.h"
#include "WaveSource.h"

namespace ni::ui
{

/* ---------------------------------------------------------------- cell -- */

/*
 * A step that hands every event to its grid: the press, its drag and its
 * release, the keys, and the focus it reports -- so the grid is the one
 * place a gesture lives, as the web pads' padGesture was.
 */
class StepGrid::Cell final : public Step
{
public:
    Cell (StepGrid& g, int i) : grid (g), index (i)
    {
        setWantsKeyboardFocus (false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        focus.pointerUsed();
        grid.pressed (index, e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { grid.dragged (index, e); }
    void mouseUp (const juce::MouseEvent&) override { grid.released (index); }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (! grid.keyed (index, k))
            return false;
        focus.keyUsed();
        grid.repaint();
        relight (grid);
        return true;
    }

    void focusGained (FocusChangeType cause) override
    {
        focus.focusGained (cause);
        grid.repaint();
        relight (grid);
    }
    void focusLost (FocusChangeType) override
    {
        focus.focusLost();
        grid.repaint();
        relight (grid);
    }

    bool isFocusShown() const { return focus.isVisible(); }
    /* The keys moved the focus here: it shows, as the key's own would. */
    void keyArrived() { focus.keyUsed(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::cell,
            juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press, [this]
            {
                if (grid.onToggle)
                    grid.onToggle (index, false);
            }));
    }

private:
    StepGrid& grid;
    const int index;
    FocusVisibility focus { *this };
};

/* ---------------------------------------------------------------- grid -- */

StepGrid::StepGrid()
{
    setTitle ("Steps");
    setWaveSource (*this);
    /* The gaps between steps are the grid's: they show its line too, and a
     * press there is on nothing. */
    setInterceptsMouseClicks (true, true);

    for (int i = 0; i < maxSteps; ++i)
    {
        auto cell = std::make_unique<Cell> (*this, i);
        addChildComponent (*cell);
        cells.push_back (std::move (cell));
    }
    setCount (count);
    cells.front()->setWantsKeyboardFocus (true);
    setSize (width, heightFor (count));
}

StepGrid::~StepGrid() = default;

int StepGrid::rowsFor (int n) noexcept
{
    return juce::jmax (1, (juce::jlimit (1, maxSteps, n) + columns - 1) / columns);
}

int StepGrid::heightFor (int n) noexcept
{
    const int rows = rowsFor (n);
    return rows * Step::size + (rows - 1) * gap;
}

void StepGrid::setCount (int n)
{
    n = juce::jlimit (1, maxSteps, n);
    for (int i = 0; i < maxSteps; ++i)
        cells[(size_t) i]->setVisible (i < n);
    count = n;
    if (focused >= count)
        moveFocus (count - 1);
    refreshNames();
}

void StepGrid::setState (int index, const Step::State& s)
{
    if (index >= 0 && index < maxSteps)
        cells[(size_t) index]->setState (s);
}

const Step::State& StepGrid::getState (int index) const
{
    return cells[(size_t) juce::jlimit (0, maxSteps - 1, index)]->getState();
}

Step& StepGrid::step (int index) const
{
    return *cells[(size_t) juce::jlimit (0, maxSteps - 1, index)];
}

juce::Rectangle<int> StepGrid::stepBounds (int index) const
{
    const int col = index % columns, row = index / columns;
    return { col * (Step::size + gap), row * (Step::size + gap), Step::size, Step::size };
}

int StepGrid::stepAt (juce::Point<int> p) const
{
    for (int i = 0; i < count; ++i)
        if (stepBounds (i).contains (p))
            return i;
    return -1;
}

void StepGrid::moveFocus (int index)
{
    index = juce::jlimit (0, count - 1, index);
    if (index != focused)
    {
        cells[(size_t) focused]->setWantsKeyboardFocus (false);
        focused = index;
    }
    auto& cell = *cells[(size_t) focused];
    cell.setWantsKeyboardFocus (true);
    if (cell.isShowing())
        cell.grabKeyboardFocus();
}

void StepGrid::setStepInfo (const juce::String& line)
{
    setInfo (*this, line);
    for (auto& c : cells)
        c->setDescription (line);
}

void StepGrid::refreshNames()
{
    for (int i = 0; i < count; ++i)
    {
        const auto name = describe ? describe (i) : "Step " + juce::String (i + 1);
        auto& cell = *cells[(size_t) i];
        if (cell.getTitle() == name)
            continue;
        cell.setTitle (name);
        if (auto* handler = cell.getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);
    }
}

/* ------------------------------------------------------------- gesture -- */

void StepGrid::pressed (int index, const juce::MouseEvent& e)
{
    /* The step pressed takes the Tab stop, as the web pads' focus followed a
     * click -- quietly: a click shows no ring. */
    if (index != focused)
    {
        cells[(size_t) focused]->setWantsKeyboardFocus (false);
        focused = index;
        cells[(size_t) focused]->setWantsKeyboardFocus (true);
    }

    pressedStep = index;
    dragging = false;
    mayDrag = onPress ? onPress (index, e.mods.isShiftDown()) : false;
}

void StepGrid::dragged (int index, const juce::MouseEvent& e)
{
    if (index != pressedStep || ! mayDrag)
        return;
    if (! dragging)
    {
        if (std::abs (e.position.y - e.mouseDownPosition.y) < dragDeadZone)
            return;
        dragging = true;
    }
    /* Against the pressed step's own box, followed past its edges. */
    const float amount = 1.0f - e.position.y / (float) Step::size;
    if (onDrag)
        onDrag (index, juce::jlimit (0.0f, 1.0f, amount));
}

void StepGrid::released (int index)
{
    if (index != pressedStep)
        return;
    pressedStep = -1;
    dragging = false;
    mayDrag = false;
    if (onRelease)
        onRelease (index);
}

bool StepGrid::keyed (int index, const juce::KeyPress& k)
{
    const auto a = keys::padKey (k, index, count, columns);
    switch (a.kind)
    {
        case keys::PadAction::Kind::move:
            moveFocus (a.to);
            cells[(size_t) focused]->keyArrived();
            return true;
        case keys::PadAction::Kind::toggle:
            if (onToggle)
                onToggle (index, a.tie);
            return true;
        case keys::PadAction::Kind::depth:
            if (onNudge)
                onNudge (index, a.depth);
            return true;
        case keys::PadAction::Kind::none:
            break;
    }
    return false;
}

/* -------------------------------------------------------------- layout -- */

void StepGrid::resized()
{
    for (int i = 0; i < maxSteps; ++i)
        cells[(size_t) i]->setBounds (stepBounds (i));
}

void StepGrid::paint (juce::Graphics& g)
{
    /* No ground of its own: the window shows between the steps, and the
     * steps' light lies over it. */
    paintChildLights (g, *this);
}

void StepGrid::paintOverChildren (juce::Graphics& g)
{
    /* The focus ring round the focused step, over its neighbours' edges as
     * a box-shadow on the focused element falls. */
    const auto& cell = *cells[(size_t) focused];
    if (cell.isVisible() && cell.isFocusShown())
        uv::light::glowFocus (g, cell.getBounds().toFloat());
}

void StepGrid::paintLight (juce::Graphics& g)
{
    forwardChildLights (g, *this);
    const auto& cell = *cells[(size_t) focused];
    if (cell.isVisible() && cell.isFocusShown())
    {
        excludeOwnBounds (g, *this);
        uv::light::glowFocus (g, cell.getBounds().toFloat());
    }
}

std::unique_ptr<juce::AccessibilityHandler> StepGrid::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

} // namespace ni::ui

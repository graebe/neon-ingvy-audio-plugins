// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A row of sequencer steps that wraps: the StepGrid card of Ultraviolet 1.1.0,
 * and the Trance Gate web editor's StepGrid.jsx.
 *
 * SIXTEEN Steps A ROW, 40px each, space-2 (8px) apart -- 760px, which is why
 * the windows that have one are 760px across inside their padding -- and as
 * many rows as the count needs, each a full row but the last. The grid never
 * scales a step: a longer pattern is more rows. Every fourth step is the
 * first of a beat and carries the line-200 border (Step::State::beat), so
 * bars read without numbers.
 *
 * THE POINTER, from the Step card and the web pads (lib/steps.js):
 *
 *   press      onPress (step, shift), at once -- a click activates fully,
 *              wherever in the step it lands; the owner says whether a drag
 *              may follow
 *   drag       after 4px of vertical travel from the press, and not before
 *              -- a click is not a drag, however much the hand shakes --
 *              onDrag (step, amount) with amount = 1 - y/h against the
 *              pressed step's own box, followed past its edges
 *   release    onRelease (step): the end of the gesture, whether it dragged
 *
 * THE KEYBOARD: one Tab stop for the whole grid -- the step last touched --
 * and padKey's map (Keys.h): the arrows move between steps, a row at a time
 * up and down; Enter is onToggle (step, tie) with Shift for a tie;
 * Alt with Up or Down is onNudge (step, delta), 10 % (Shift: 1 %). Its focus
 * ring is glow-focus round the focused step, for the keyboard only.
 *
 * IT KNOWS NOTHING ABOUT A PATTERN. The owner sets each step's State and
 * decides what every gesture does to its model; the grid keeps no copy of a
 * step's state beyond what it draws. To a screen reader the grid is a group
 * named by its title ("Steps"), and each step a cell named by describe(step)
 * ("Step 3, on, 80 %") with a press action that toggles it.
 *
 * Solid to the Ground's rings (WaveSource.h). Its steps' light falls in the
 * gaps between them and past its edges, so it is Luminous (ChildLights.h).
 */
#pragma once

#include "Step.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace ni::ui
{

class StepGrid : public juce::Component,
                 public Luminous
{
public:
    static constexpr int columns = 16;
    static constexpr int gap = 8;
    static constexpr int width = columns * Step::size + (columns - 1) * gap;
    /* A press is a drag only after this much travel. */
    static constexpr float dragDeadZone = 4.0f;
    /* The largest count it holds. */
    static constexpr int maxSteps = 128;

    StepGrid();
    ~StepGrid() override;

    /* The rows `count` steps take, and their height. */
    static int rowsFor (int count) noexcept;
    static int heightFor (int count) noexcept;

    /* How many steps it shows, 1..maxSteps: the rest are hidden. Resizes
     * nothing; the owner gives it heightFor(). */
    void setCount (int);
    int getCount() const noexcept { return count; }

    /* What step `index` shows. Repaints only what changed. */
    void setState (int index, const Step::State&);
    const Step::State& getState (int index) const;

    /* A step, as a component: for a test, or for an owner that puts
     * something of its own on it (the Trance Gate's arrival number). */
    Step& step (int index) const;
    juce::Rectangle<int> stepBounds (int index) const;
    /* The step at a point in the grid's coordinates, or -1. */
    int stepAt (juce::Point<int>) const;

    /* The step with the grid's one Tab stop. moveFocus gives it the keyboard
     * too, when it is showing to take it. */
    int focusedStep() const noexcept { return focused; }
    void moveFocus (int index);

    /* Each step's accessible name. */
    std::function<juce::String (int step)> describe;

    /* The grid's info line, also each step's description. */
    void setStepInfo (const juce::String& line);

    /* ---- what it asks for */
    std::function<bool (int step, bool shift)> onPress;
    std::function<void (int step, float amount)> onDrag;
    std::function<void (int step)> onRelease;
    std::function<void (int step, bool tie)> onToggle;
    std::function<void (int step, float delta)> onNudge;

    /* The names read again: describe() changed its mind. */
    void refreshNames();

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;

private:
    class Cell;

    void pressed (int index, const juce::MouseEvent&);
    void dragged (int index, const juce::MouseEvent&);
    void released (int index);
    bool keyed (int index, const juce::KeyPress&);
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    std::vector<std::unique_ptr<Cell>> cells;
    int count = 16;
    int focused = 0;

    /* The gesture under way. */
    int pressedStep = -1;
    bool dragging = false;
    bool mayDrag = false;

    JUCE_DECLARE_NON_COPYABLE (StepGrid)
};

} // namespace ni::ui

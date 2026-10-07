// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The pads: the Trance Gate's pattern as the kit's StepGrid, sixteen to a row
 * and as many rows as Length needs -- the web editor's StepGrid.jsx.
 *
 * WHAT A PAD SHOWS is the Step card's rule, THE BORDER IS WHAT YOU DREW, THE
 * FILL IS WHAT YOU HEAR, from the pattern the engine published:
 *
 *   drawn      off, on or tie
 *   amount     the step's amount
 *   level      the engine's level factor from the fade (Pattern::levels), so
 *              a step the fade has not brought in keeps its border and no
 *              fill, and a hole Fade Out has not removed yet is lit with no
 *              border -- neither mistaken for a step you drew
 *   play       the step the transport is on, while it runs
 *   beat       every fourth step, so bars read without numbers
 *   cursor     the step being edited (the engine's: the last one set)
 *   waiting    in Set order, an arriving step not named yet this pass
 *   number     the step's arrival, only while it means something: in Set
 *              order, or while the fade is part way in -- and only on the
 *              kind the fade brings in, the hits or under Fade Out the holes.
 *              A control, so ink over the well where the card's plain index
 *              is ink-dim (Step.h)
 *
 * THE NUMBER IS A CONTROL. A press on it opens a field over the pad's corner
 * to type a new place (EditField: Enter or a click away keeps it, Escape
 * abandons it), and the engine swaps the two steps when the place is taken.
 * Its own component takes that press, so the pad under it is not toggled and
 * a drag is not measured from it, and it says what it does in the hint bar.
 * FROM THE KEYBOARD, as 1.1.0 asks of every pointer control (the web pads
 * had no way): a digit typed on a focused pad whose number shows opens the
 * field with that digit in it, and the keyboard goes back to the pad when
 * the field closes.
 *
 * EVERY GESTURE IS StepEdits'S: a press, a drag for the amount, the keys. The
 * pads' line follows Set order, because a click there names an arrival then.
 *
 * Its light -- the steps' glow, the cursor, the field's focus ring -- reaches
 * past it, so it is Luminous. Message thread.
 */
#pragma once

#include "Model.h"
#include "StepEdits.h"

#include "EditField.h"
#include "Luminous.h"
#include "StepGrid.h"

#include <array>
#include <memory>

namespace ni::tg
{

class Pads final : public juce::Component,
                   public ni::ui::Luminous
{
public:
    /* The arrival field's size, over a pad's top-left corner. */
    static constexpr int fieldWidth = 20;
    static constexpr int fieldHeight = 14;

    explicit Pads (StepEdits&);
    ~Pads() override;

    /* Its height for `count` steps: the grid's. */
    static int heightFor (int count) { return ni::ui::StepGrid::heightFor (count); }

    /* The pattern, the step being played (-1 for none) and whether the fade
     * is part way in. Repaints only what changed. */
    void show (const Pattern&, int playStep, bool fading);

    ni::ui::StepGrid& grid() noexcept { return steps; }

    /* Step `index`'s arrival number as a control, or nullptr while none
     * shows. */
    juce::Component* arrival (int index);

    /* Opens the field on step `index`'s number -- or, given `typed`, with
     * that in it, the caret after it -- or closes it. */
    void editArrival (int index, const juce::String& typed = {});
    int editingArrival() const noexcept { return editing; }
    ni::ui::EditField& arrivalField() noexcept { return field; }

    /* A digit on a focused pad, which its grid passes on. */
    bool keyPressed (const juce::KeyPress&) override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;

private:
    class Arrival;

    void closeField();

    StepEdits& edits;
    ni::ui::StepGrid steps;
    std::array<std::unique_ptr<Arrival>, maxSteps> numbers;
    ni::ui::EditField field { uv::tok::type::hint };
    int editing = -1;
    bool editingFromKeys = false;

    /* What the steps' names were made from. */
    std::array<StepMode, maxSteps> modes {};
    std::array<float, maxSteps> depths {};
    bool padsOrderLine = false;

    JUCE_DECLARE_NON_COPYABLE (Pads)
};

} // namespace ni::tg

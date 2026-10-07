// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate's pads and the fade, held to the manual (docs/live.md,
 * Editing a step, The keyboard, Controls: Fade; README.md, The fade) and the
 * web editor's lib/steps.js:
 *
 *   a press       toggles fully, Shift cycles into a tie, a dead step comes
 *                 back at its full amount
 *   a drag        the amount, after 4px, against the pad; to the floor off
 *   the keys      Space or Enter a press, Alt with Up or Down the amount
 *   the fade      the border is what you drew, the fill is what you hear;
 *                 the numbers only while they mean something
 *   Set order     a press names the next arrival, of the arriving kind only
 *   a number      typed into, swapping by the engine; Escape abandons it
 *
 * Every gesture is held to what the editor asks the engine for: the fake
 * logs each command and shows it only in the next snapshot, as the plugin
 * will.
 */
#include "TranceGateEditor.h"

#include "InfoLines.h"
#include "Pointer.h"
#include "trance-gate_fakes.h"

#include <doctest.h>

using namespace ni::tg;
using namespace ni::tg::test;
using ni::ui::gallery::key;
using ni::ui::gallery::Pointer;

namespace
{
struct Rig
{
    double ms = 0.0;
    FakeModel model;
    FakeClipboard clipboard;
    FakeFilePanels panels;
    TranceGateEditor editor { model, clipboard, panels, [this] { return ms; } };

    Rig()
    {
        editor.frame().ground().setReducedMotionQuery ([] { return false; });
        model.edits.clear();
        model.params.clear();
    }

    void frame()
    {
        model.publish();
        ms += 1000.0 / 60.0;
        editor.tick (ms);
    }

    ni::ui::StepGrid& grid() { return editor.pads().grid(); }
    ni::ui::Step& pad (int i) { return grid().step (i); }
    const ni::ui::Step::State& state (int i) { return grid().getState (i); }
};

constexpr auto shift = juce::ModifierKeys::shiftModifier;
constexpr auto alt = juce::ModifierKeys::altModifier;
} // namespace

/* ------------------------------------------------------------ a press -- */

TEST_CASE ("trance-gate pads: a click toggles a step fully, wherever on the pad it lands")
{
    Rig rig;
    Pointer p;
    /* Step 1 is off: on, at its full amount, however low on the pad. */
    p.click (rig.pad (1), { 20.0f, 38.0f });
    CHECK (rig.model.takeEdits() == "step 1 on, depth 1 1.00");
    /* Step 0 is on: off. */
    p.click (rig.pad (0), { 20.0f, 2.0f });
    CHECK (rig.model.takeEdits() == "step 0 off");
}

TEST_CASE ("trance-gate pads: Shift turns an on step into a tie, and a tie or an off step on")
{
    Rig rig;
    rig.model.next.steps[4] = StepMode::tie;
    rig.frame();
    Pointer p;
    p.click (rig.pad (2), { 20.0f, 20.0f }, shift);
    CHECK (rig.model.takeEdits() == "step 2 tie");
    p.click (rig.pad (4), { 20.0f, 20.0f }, shift);
    CHECK (rig.model.takeEdits() == "step 4 on");
    p.click (rig.pad (3), { 20.0f, 20.0f }, shift);
    CHECK (rig.model.takeEdits() == "step 3 on, depth 3 1.00");
    /* A plain click on a tie turns it off. */
    rig.model.next.steps[6] = StepMode::tie;
    rig.frame();
    p.click (rig.pad (6), { 20.0f, 20.0f });
    CHECK (rig.model.takeEdits() == "step 6 off");
}

TEST_CASE ("trance-gate pads: a drag sets the amount after 4px, against the pad's own box")
{
    Rig rig;
    Pointer p;
    p.down (rig.pad (2), { 20.0f, 20.0f });
    CHECK (rig.model.takeEdits() == "step 2 off");
    /* Under 4px of travel it is still a click. */
    p.drag ({ 20.0f, 17.0f });
    CHECK (rig.model.takeEdits().empty());
    /* Then the amount is the height on the pad: the press turned it off, so
     * the drag turns it on again -- once, though the snapshot still says off. */
    p.drag ({ 20.0f, 10.0f });
    p.drag ({ 20.0f, 30.0f });
    CHECK (rig.model.takeEdits() == "step 2 on, depth 2 0.75, depth 2 0.25");
    /* Past the pad it follows; to the floor, off -- once. */
    p.drag ({ 20.0f, 39.5f });
    p.drag ({ 20.0f, 60.0f });
    CHECK (rig.model.takeEdits() == "step 2 off");
    p.drag ({ 20.0f, -20.0f });
    p.up();
    CHECK (rig.model.takeEdits() == "step 2 on, depth 2 1.00");
}

TEST_CASE ("trance-gate pads: a tie dragged keeps being a tie")
{
    Rig rig;
    Pointer p;
    p.down (rig.pad (0), { 20.0f, 20.0f }, shift);
    p.drag ({ 20.0f, 30.0f });
    p.up();
    CHECK (rig.model.takeEdits() == "step 0 tie, depth 0 0.25");
}

/* ------------------------------------------------------------- the keys -- */

TEST_CASE ("trance-gate pads: one Tab stop; arrows move, Space or Enter press, Alt with Up or Down the amount")
{
    Rig rig;
    auto& grid = rig.grid();
    CHECK (grid.focusedStep() == 0);
    CHECK (key (rig.pad (0), juce::KeyPress::rightKey));
    CHECK (grid.focusedStep() == 1);
    CHECK (key (rig.pad (1), juce::KeyPress::spaceKey));
    CHECK (rig.model.takeEdits() == "step 1 on, depth 1 1.00");
    CHECK (key (rig.pad (2), juce::KeyPress::returnKey, shift));
    CHECK (rig.model.takeEdits() == "step 2 tie");

    /* Alt+Down twice takes a step to 80 % (the manual's example), one key at
     * a time, each read from the snapshot the last one made. */
    CHECK (key (rig.pad (8), juce::KeyPress::downKey, alt));
    CHECK (rig.model.takeEdits() == "depth 8 0.90");
    rig.frame();
    CHECK (key (rig.pad (8), juce::KeyPress::downKey, alt));
    CHECK (rig.model.takeEdits() == "depth 8 0.80");
    /* Shift is 1 %. */
    rig.frame();
    CHECK (key (rig.pad (8), juce::KeyPress::upKey, alt | shift));
    CHECK (rig.model.takeEdits() == "depth 8 0.81");
    /* An off step raised comes on; one lowered to the floor goes off. */
    CHECK (key (rig.pad (9), juce::KeyPress::upKey, alt));
    CHECK (rig.model.takeEdits() == "step 9 on, depth 9 1.00");
    rig.model.next.depths[10] = 0.1f;
    rig.frame();
    CHECK (key (rig.pad (10), juce::KeyPress::downKey, alt));
    CHECK (rig.model.takeEdits() == "step 10 off");
}

TEST_CASE ("trance-gate pads: each pad is named as the screen reader hears it")
{
    Rig rig;
    rig.model.next.steps[1] = StepMode::tie;
    rig.model.next.depths[2] = 0.8f;
    rig.frame();
    CHECK (rig.pad (0).getTitle() == "Step 1, on, 100 %");
    CHECK (rig.pad (1).getTitle() == "Step 2, tie, 100 %");
    CHECK (rig.pad (2).getTitle() == "Step 3, on, 80 %");
    CHECK (rig.pad (3).getTitle() == "Step 4, off");
}

/* ---------------------------------------------------------- what shows -- */

TEST_CASE ("trance-gate pads: a pad shows the pattern -- amount, tie, cursor, beats -- with the playhead over it")
{
    Rig rig;
    rig.model.next.steps[1] = StepMode::tie;
    rig.model.next.depths[2] = 0.5f;
    rig.model.next.cursor = 2;
    rig.model.engineTransport = { true, 6.5, 125.0 };
    rig.frame();
    CHECK (rig.state (0).drawn == ni::ui::Step::Drawn::on);
    CHECK (rig.state (1).drawn == ni::ui::Step::Drawn::tie);
    CHECK (rig.state (2).amount == doctest::Approx (0.5f));
    CHECK (rig.state (2).cursor);
    CHECK_FALSE (rig.state (0).cursor);
    CHECK (rig.state (0).beat);
    CHECK (rig.state (4).beat);
    CHECK_FALSE (rig.state (5).beat);
    CHECK (rig.state (6).play);
    /* At rest there are no numbers: the beat borders say where the bars are. */
    for (int i = 0; i < 16; ++i)
        CHECK (rig.state (i).number == 0);
}

TEST_CASE ("trance-gate pads: the border is what you drew, the fill is what you hear -- Fade In and Out")
{
    Rig rig;
    /* Fade In at 50 %: of the eight hits ranked 1..8, four have arrived. */
    rig.model.set (param::fade, 50.0f);
    rig.frame();
    CHECK (rig.state (0).level == doctest::Approx (1.0f));     // rank 1
    CHECK (rig.state (14).level == doctest::Approx (0.0f));    // rank 8: drawn, not arrived
    CHECK (rig.editor.pads().grid().step (14).isPending());
    CHECK_FALSE (rig.editor.pads().grid().step (1).isFilled());
    /* While the fade is part way in, the hits carry their numbers, the
     * holes none. */
    CHECK (rig.state (14).number == 8);
    CHECK (rig.state (1).number == 0);

    /* Fade Out: the holes arrive; one not yet removed still sounds. */
    rig.model.set (param::fadeDir, 1.0f);
    rig.frame();
    CHECK (rig.editor.pads().grid().step (15).isFilled());   // hole rank 8: still sounding
    CHECK_FALSE (rig.editor.pads().grid().step (1).isFilled()); // hole rank 1: removed
    CHECK (rig.state (15).number == 8);
    CHECK (rig.state (0).number == 0);

    /* At 100 % the pattern is the pattern, and the numbers go. */
    rig.model.set (param::fade, 100.0f);
    rig.frame();
    CHECK (rig.state (15).number == 0);
    CHECK_FALSE (rig.editor.pads().grid().step (15).isFilled());
}

/* ------------------------------------------------------------ Set order -- */

TEST_CASE ("trance-gate Set order: a press names the next arrival; the button counts how far in you are")
{
    Rig rig;
    auto& order = rig.editor.orderButton();
    CHECK (order.getText() == "Set order");
    order.onClick();
    CHECK (order.isOn());
    CHECK (order.getText() == "Set order 0/8");
    /* The numbers show, and every hit waits to be named. */
    CHECK (rig.state (6).number == 4);
    CHECK (rig.state (6).waiting);
    CHECK_FALSE (rig.state (1).waiting);

    Pointer p;
    p.click (rig.pad (6), { 20.0f, 20.0f });
    p.click (rig.pad (2), { 20.0f, 20.0f });
    CHECK (rig.model.takeEdits() == "order 6 1, order 2 2");
    rig.frame();
    CHECK (order.getText() == "Set order 2/8");
    CHECK_FALSE (rig.state (6).waiting);

    /* A hole is not a hit: nothing. A step already named keeps its place. */
    p.click (rig.pad (3), { 20.0f, 20.0f });
    p.click (rig.pad (6), { 20.0f, 20.0f });
    CHECK (rig.model.takeEdits().empty());
    /* A press names; it never drags. */
    p.down (rig.pad (8), { 20.0f, 20.0f });
    p.drag ({ 20.0f, 0.0f });
    p.up();
    CHECK (rig.model.takeEdits() == "order 8 3");

    /* The ring names too, and does not sweep. */
    auto& ring = rig.editor.ring();
    const auto b = ring.band();
    const float r = (b.inner + b.outer) * 0.5f;
    const auto wedge = [&] (int i)
    {
        const float a = juce::MathConstants<float>::twoPi * ((float) i + 0.5f) / 16.0f;
        return juce::Point<float> (b.centre + std::sin (a) * r, b.centre - std::cos (a) * r);
    };
    p.down (ring, wedge (10));
    p.drag (wedge (12));
    p.up();
    CHECK (rig.model.takeEdits() == "order 10 4");
    /* The keyboard's press names as well. */
    CHECK (key (rig.pad (12), juce::KeyPress::spaceKey));
    CHECK (rig.model.takeEdits() == "order 12 5");

    /* Pressed again, the mode ends; on again, a new pass from 1. */
    order.onClick();
    CHECK_FALSE (order.isOn());
    CHECK (order.getText() == "Set order");
    order.onClick();
    rig.frame();
    CHECK (order.getText() == "Set order 0/8");
    p.click (rig.pad (0), { 20.0f, 20.0f });
    CHECK (rig.model.takeEdits() == "order 0 1");
}

TEST_CASE ("trance-gate Set order: under Fade Out it is the holes that are named")
{
    Rig rig;
    rig.model.set (param::fadeDir, 1.0f);
    rig.frame();
    rig.editor.orderButton().onClick();
    CHECK (rig.state (1).waiting);
    CHECK_FALSE (rig.state (0).waiting);
    Pointer p;
    p.click (rig.pad (0), { 20.0f, 20.0f });
    p.click (rig.pad (1), { 20.0f, 20.0f });
    CHECK (rig.model.takeEdits() == "order 1 1");
}

TEST_CASE ("trance-gate Shuffle order deals a new arrival order, the pattern left alone")
{
    Rig rig;
    rig.editor.shuffleButton().onClick();
    CHECK (rig.model.takeEdits() == "shuffle");
}

/* -------------------------------------------------------------- a number -- */

TEST_CASE ("trance-gate pads: click a number to type one; Enter keeps it, Escape leaves it")
{
    Rig rig;
    rig.model.set (param::fade, 50.0f);
    rig.frame();
    auto& pads = rig.editor.pads();
    auto* number = pads.arrival (6);
    REQUIRE (number != nullptr);
    /* It sits on the pad's corner and takes the press, not the pad. */
    CHECK (pads.grid().stepBounds (6).contains (number->getBounds().getPosition()));
    Pointer p;
    p.click (*number, { 3.0f, 3.0f });
    CHECK (rig.model.takeEdits().empty());
    auto& field = pads.arrivalField();
    CHECK (field.isOpen());
    CHECK (field.isVisible());
    CHECK (field.getText() == "4");
    CHECK (field.getBounds() == pads.grid().stepBounds (6).withSize (Pads::fieldWidth, Pads::fieldHeight));

    field.setText ("2");
    field.returnPressed();
    CHECK (rig.model.takeEdits() == "order 6 2");
    CHECK_FALSE (field.isVisible());
    CHECK (pads.editingArrival() == -1);

    p.click (*pads.arrival (6), { 3.0f, 3.0f });
    field.setText ("7");
    field.escapePressed();
    CHECK (rig.model.takeEdits().empty());

    /* What is not a place changes nothing. */
    p.click (*pads.arrival (6), { 3.0f, 3.0f });
    field.setText ("x");
    field.returnPressed();
    p.click (*pads.arrival (6), { 3.0f, 3.0f });
    field.setText ("0");
    field.returnPressed();
    CHECK (rig.model.takeEdits().empty());
}

TEST_CASE ("trance-gate pads: a number opened on one pad and then another keeps the first")
{
    Rig rig;
    rig.editor.orderButton().onClick();
    auto& pads = rig.editor.pads();
    Pointer p;
    p.click (*pads.arrival (0), { 3.0f, 3.0f });
    pads.arrivalField().setText ("5");
    p.click (*pads.arrival (2), { 3.0f, 3.0f });
    CHECK (rig.model.takeEdits() == "order 0 5");
    CHECK (pads.editingArrival() == 2);
}

/* ------------------------------------------------------------- snapshots -- */

TEST_CASE ("trance-gate pads: the editor never reads an edit back before the engine took it")
{
    Rig rig;
    const int before = rig.model.patternReads;
    Pointer p;
    p.down (rig.pad (1), { 20.0f, 20.0f });
    p.drag ({ 20.0f, 4.0f });
    p.drag ({ 20.0f, 8.0f });
    p.up();
    /* One press read the step it pressed; the drag read nothing. */
    CHECK (rig.model.patternReads - before <= 2);
    CHECK (rig.model.takeEdits() == "step 1 on, depth 1 1.00, depth 1 0.90, depth 1 0.80");
}

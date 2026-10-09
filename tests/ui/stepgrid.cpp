// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Step and the StepGrid: what a state comes to (the border is what was
 * drawn, the fill what is heard), sixteen to a row with rows that grow, a
 * press that is a press, a drag only past its dead zone and measured against
 * the pressed step, one Tab stop and padKey's map, names for a screen
 * reader. Then the states, as a picture.
 */
#include "Gallery.h"
#include "Pointer.h"
#include "StepGrid.h"
#include "UvTokens.h"
#include "checks.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Step;
using ni::ui::StepGrid;
using ni::ui::gallery::Pointer;
using ni::ui::gallery::key;

namespace
{
Step::State drawn (Step::Drawn d, float amount = 1.0f, float level = -1.0f)
{
    Step::State s;
    s.drawn = d;
    s.amount = amount;
    s.level = level < 0.0f ? Step::levelAsDrawn (d) : level;
    return s;
}

/* A grid whose owner records what it is asked. */
struct Rig
{
    Rig()
    {
        grid.setCount (16);
        grid.setSize (StepGrid::width, StepGrid::heightFor (16));
        grid.onPress = [this] (int i, bool shift)
        {
            log.add ("press " + juce::String (i) + (shift ? " shift" : ""));
            return allowDrag;
        };
        grid.onDrag = [this] (int i, float a) { log.add ("drag " + juce::String (i) + " " + juce::String (a, 2)); };
        grid.onRelease = [this] (int i) { log.add ("release " + juce::String (i)); };
        grid.onToggle = [this] (int i, bool tie) { log.add ("toggle " + juce::String (i) + (tie ? " tie" : "")); };
        grid.onNudge = [this] (int i, float d) { log.add ("nudge " + juce::String (i) + " " + juce::String (d, 2)); };
    }

    juce::String said() const { return log.joinIntoString (", "); }

    StepGrid grid;
    juce::StringArray log;
    bool allowDrag = true;
};
} // namespace

TEST_CASE ("step: the border is what was drawn, the fill is what is heard")
{
    Step s;
    s.setState (drawn (Step::Drawn::on, 0.5f));
    CHECK (s.isLit());
    CHECK (s.litHeight() == doctest::Approx (0.5f));
    CHECK_FALSE (s.isPending());

    /* A fade part way: the lit height is amount x level, never under 5 %. */
    s.setState (drawn (Step::Drawn::on, 0.5f, 0.5f));
    CHECK (s.litHeight() == doctest::Approx (0.25f));
    s.setState (drawn (Step::Drawn::on, 0.02f, 1.0f));
    CHECK (s.litHeight() == doctest::Approx (Step::minLit));

    /* Drawn on, not arrived: pending -- in the pattern, not sounding. */
    s.setState (drawn (Step::Drawn::on, 1.0f, 0.0f));
    CHECK (s.isPending());
    CHECK_FALSE (s.isLit());

    /* Drawn off, still sounding: filled, lit to the level whatever the amount. */
    s.setState (drawn (Step::Drawn::off, 0.3f, 0.75f));
    CHECK (s.isFilled());
    CHECK (s.litHeight() == doctest::Approx (0.75f));

    /* A tie is a bar, never a fill; a tie the fade has not reached is pending. */
    s.setState (drawn (Step::Drawn::tie));
    CHECK_FALSE (s.isLit());
    s.setState (drawn (Step::Drawn::tie, 1.0f, 0.0f));
    CHECK (s.isPending());
}

TEST_CASE ("step: a number takes the colour that reads on what is behind it")
{
    namespace c = uv::tok::colour;
    Step s;
    /* Where the number's glyphs sit: the top of its 14px box at y = 2. */
    constexpr float glyphs = 8.0f;

    /* A full fill: on-uv. */
    auto state = drawn (Step::Drawn::on);
    state.number = 3;
    state.numberIsControl = true;
    s.setState (state);
    CHECK (s.numberColourAt (glyphs) == c::onUv);

    /* A fill that stops below the number, a tie, a step waiting for the
     * fade: the bare well, so ink -- never on-uv on bg-200. */
    for (const auto& below : { drawn (Step::Drawn::on, 0.4f), drawn (Step::Drawn::tie),
                               drawn (Step::Drawn::on, 1.0f, 0.0f) })
    {
        auto st = below;
        st.number = 3;
        st.numberIsControl = true;
        s.setState (st);
        CHECK (s.numberColourAt (glyphs) == c::ink);
    }
    /* Part way up the number: split at the fill's top edge. */
    state.amount = 0.8f;
    s.setState (state);
    CHECK (s.fillTop() == doctest::Approx (39.0f - 38.0f * 0.8f));
    CHECK (s.numberColourAt (s.fillTop() - 1.0f) == c::ink);
    CHECK (s.numberColourAt (s.fillTop() + 1.0f) == c::onUv);

    /* A hole lit at filledAlpha: bg-000, at 5.7:1 on it. */
    auto hole = drawn (Step::Drawn::off, 1.0f, 1.0f);
    hole.number = 5;
    hole.numberIsControl = true;
    s.setState (hole);
    CHECK (s.isFilled());
    CHECK (s.numberColourAt (glyphs) == c::bg000);

    /* The card's plain index is a mark: ink-dim off the fill. */
    auto index = drawn (Step::Drawn::off);
    index.number = 4;
    s.setState (index);
    CHECK (s.numberColourAt (glyphs) == c::inkDim);
}

TEST_CASE ("step grid: sixteen to a row, 8 apart, 760 wide, rows that grow")
{
    CHECK (StepGrid::width == 760);
    CHECK (StepGrid::rowsFor (1) == 1);
    CHECK (StepGrid::rowsFor (16) == 1);
    CHECK (StepGrid::rowsFor (17) == 2);
    CHECK (StepGrid::rowsFor (128) == 8);
    CHECK (StepGrid::heightFor (16) == 40);
    CHECK (StepGrid::heightFor (128) == 8 * 40 + 7 * 8);

    Rig rig;
    rig.grid.setCount (20);
    rig.grid.setSize (StepGrid::width, StepGrid::heightFor (20));
    CHECK (rig.grid.stepBounds (15) == juce::Rectangle<int> (720, 0, 40, 40));
    CHECK (rig.grid.stepBounds (16) == juce::Rectangle<int> (0, 48, 40, 40));
    CHECK (rig.grid.step (19).isVisible());
    CHECK_FALSE (rig.grid.step (20).isVisible());
    CHECK (rig.grid.stepAt ({ 44, 10 }) == -1);   // a gap is no step
    CHECK (rig.grid.stepAt ({ 50, 10 }) == 1);
}

TEST_CASE ("step grid: a press at once; a drag only after 4px, against the pressed step's box")
{
    Rig rig;
    auto& s = rig.grid.step (3);
    Pointer p;
    p.down (s, { 20.0f, 30.0f }, juce::ModifierKeys::shiftModifier);
    p.drag ({ 20.0f, 27.0f });          // 3px: still a click
    p.drag ({ 21.0f, 10.0f });          // past the dead zone: 1 - 10/40
    p.drag ({ 21.0f, -20.0f });         // followed past the edge, clamped
    p.up();
    CHECK (rig.said() == "press 3 shift, drag 3 0.75, drag 3 1.00, release 3");

    /* A press the owner keeps for itself (a mode) is not a drag. */
    rig.log.clear();
    rig.allowDrag = false;
    p.down (s, { 20.0f, 20.0f });
    p.drag ({ 20.0f, 0.0f });
    p.up();
    CHECK (rig.said() == "press 3, release 3");
    /* The step pressed has the Tab stop. */
    CHECK (rig.grid.focusedStep() == 3);
}

TEST_CASE ("step grid: one Tab stop, the arrows move it, Enter toggles, Alt the amount")
{
    Rig rig;
    auto& first = rig.grid.step (0);
    CHECK (first.getWantsKeyboardFocus());
    CHECK_FALSE (rig.grid.step (1).getWantsKeyboardFocus());

    CHECK (key (first, juce::KeyPress::rightKey));
    CHECK (rig.grid.focusedStep() == 1);
    CHECK (rig.grid.step (1).getWantsKeyboardFocus());
    CHECK_FALSE (first.getWantsKeyboardFocus());

    auto& second = rig.grid.step (1);
    CHECK_FALSE (key (second, juce::KeyPress::spaceKey));
    CHECK (key (second, juce::KeyPress::returnKey));
    CHECK (key (second, juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier));
    CHECK (key (second, juce::KeyPress::downKey, juce::ModifierKeys::altModifier));
    CHECK (key (second, juce::KeyPress::upKey,
                juce::ModifierKeys (juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier)));
    CHECK (rig.said() == "toggle 1, toggle 1 tie, nudge 1 -0.10, nudge 1 0.01");

    /* A count that shrinks under the Tab stop takes it along. */
    rig.grid.moveFocus (15);
    rig.grid.setCount (8);
    CHECK (rig.grid.focusedStep() == 7);
}

TEST_CASE ("step grid: a group of cells, each named by its owner, pressed by a screen reader")
{
    Rig rig;
    rig.grid.describe = [] (int i) { return "Step " + juce::String (i + 1) + ", off"; };
    rig.grid.refreshNames();
    CHECK (rig.grid.getTitle() == "Steps");
    CHECK (rig.grid.step (4).getTitle() == "Step 5, off");

    /* getAccessibilityHandler() answers only on screen; this is what it makes. */
    const auto handler = static_cast<juce::Component&> (rig.grid.step (4)).createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::cell);
    CHECK (handler->getActions().invoke (juce::AccessibilityActionType::press));
    CHECK (rig.said() == "toggle 4");

    const auto line = juce::String::fromUTF8 ("Steps — what they do.");
    rig.grid.setStepInfo (line);
    CHECK (rig.grid.step (4).getDescription() == line);
    CHECK (ni::ui::infoSource (&rig.grid.step (4)) == &rig.grid);
}

NI_SNAPSHOT_TEST ("step grid: every state, and what a fade adds")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("controls-step-grid")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "controls-step-grid");
}

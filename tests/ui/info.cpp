// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Info in the hint bar: in at once, out after a beat, an outcome first, the
 * pointer before the focus -- and focus only when the keyboard put it there.
 * The web kit's info.test.mjs, case for case where the case is the same, and
 * the native tracker and focus on top.
 */
#include "Focus.h"
#include "Info.h"

#include <doctest.h>

using namespace ni::ui;

namespace
{
const juce::String RATE = juce::String::fromUTF8 ("Rate \xe2\x80\x94 the length of one step, synced to the song tempo.");
const juce::String LENGTH = juce::String::fromUTF8 ("Length \xe2\x80\x94 1 to 128 steps.");

/* A clock the test moves by hand. */
struct Time
{
    double now = 0.0;
    InfoState::Clock clock() { return [this] { return now; }; }
};

/* A window with a hint bar: the root that owns the state. */
struct Window final : public juce::Component, public InfoHost
{
    explicit Window (InfoState::Clock clock) : state (std::move (clock)) {}
    InfoState& infoState() override { return state; }
    InfoState state;
};

/* A control with a FocusVisibility, wired as the kit's controls wire it. */
struct Control final : public juce::Component
{
    Control() { setWantsKeyboardFocus (true); }
    void focusGained (FocusChangeType cause) override { focus.focusGained (cause); }
    void focusLost (FocusChangeType) override { focus.focusLost(); }
    bool keyPressed (const juce::KeyPress&) override { focus.keyUsed(); return true; }
    void mouseDown (const juce::MouseEvent&) override { focus.pointerUsed(); }
    FocusVisibility focus { *this };
};

juce::MouseEvent eventFor (juce::Component& c, juce::ModifierKeys mods = {})
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, {}, mods, juce::MouseInputSource::defaultPressure,
                             juce::MouseInputSource::defaultOrientation,
                             juce::MouseInputSource::defaultRotation,
                             juce::MouseInputSource::defaultTiltX,
                             juce::MouseInputSource::defaultTiltY,
                             &c, &c, juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false);
}
} // namespace

/* ------------------------------------------------------------ the limit -- */

TEST_CASE ("info: the limit counts characters, not bytes")
{
    constexpr InfoText line { "Rate — the length of one step, synced to the song tempo." };
    static_assert (line.length() == 56, "the dash is one character");
    CHECK (line.str() == RATE);
    CHECK (utf8Length ("\xe2\x80\x94", 3) == 1);

    /* A constexpr line over 72 characters does not compile -- uncomment to see
     * infoStringIsLongerThan72Characters named in the error:
     * constexpr InfoText tooLong { "0123456789012345678901234567890123456789012345678901234567890123456789012" };
     */
    constexpr InfoText exactly72 { "Length — 1 to 128 steps; holds on bar lengths, Page Up/Down jumps there." };
    static_assert (exactly72.length() == infoLimit, "the longest line the bar takes");
}

TEST_CASE ("info: a component carries its line, and it is its accessible description")
{
    juce::Component knob;
    setInfo (knob, RATE);
    CHECK (infoOf (knob) == RATE);
    CHECK (knob.getDescription() == RATE);

    setInfo (knob, {});
    CHECK (infoOf (knob).isEmpty());
}

TEST_CASE ("info: the line that applies is the nearest one at or above a component")
{
    juce::Component panel, knob, inner;
    panel.addChildComponent (knob);
    knob.addChildComponent (inner);
    setInfo (panel, LENGTH);

    CHECK (infoSource (&inner) == &panel);
    setInfo (knob, RATE);
    CHECK (infoSource (&inner) == &knob);
    CHECK (infoSource (nullptr) == nullptr);

    CHECK (collectInfo (panel) == std::vector<juce::String> { LENGTH, RATE });
}

/* ------------------------------------------------------------ clauses -- */

TEST_CASE ("info: a line is a clause, split at its dash")
{
    const auto c = infoClause (RATE);
    CHECK (c.name == "Rate");
    CHECK (c.rest == juce::String::fromUTF8 ("\xe2\x80\x94 the length of one step, synced to the song tempo."));
    CHECK (infoClause ("Motion").name == "Motion");
    CHECK (infoClause ("Motion").rest.isEmpty());
}

TEST_CASE ("info: an outcome comes first, then the line, then the conventions")
{
    const std::vector<Clause> conventions { { "drag", "to set" }, { "double-click", "to reset" },
                                            { "click", "a readout to type" } };

    auto bar = hintClauses (conventions, {});
    CHECK (bar.clauses == conventions);
    CHECK_FALSE (bar.info.has_value());

    bar = hintClauses (conventions, RATE);
    CHECK (bar.clauses == conventions);   // still laid out underneath, hidden
    REQUIRE (bar.info.has_value());
    CHECK (bar.info->name == "Rate");

    bar = hintClauses (conventions, RATE, Clause { "Copied", "slot 1." });
    CHECK (bar.clauses.size() == 3);
    CHECK (bar.clauses[0].name == "Copied");
    CHECK (bar.clauses[1] == conventions[0]);
    CHECK_FALSE (bar.info.has_value());
}

/* -------------------------------------------------------------- timing -- */

TEST_CASE ("info: entering shows the line at once, leaving restores only after the grace")
{
    Time time;
    InfoState info (time.clock());
    int changes = 0;
    info.onChange = [&] { ++changes; };

    info.enter (RATE);
    CHECK (info.text() == RATE);
    CHECK (changes == 1);

    info.leave();
    time.now += infoGraceMs - 1;
    info.poll();
    CHECK (info.text() == RATE);   // still shown inside the grace

    time.now += 1;
    info.poll();
    CHECK (info.text().isEmpty());
    CHECK (changes == 2);
}

TEST_CASE ("info: moving between neighbours goes line to line, never through the conventions")
{
    Time time;
    InfoState info (time.clock());
    std::vector<juce::String> shown;

    info.enter (RATE);
    shown.push_back (info.text());
    info.leave();                  // across the 8px gap between two knobs
    time.now += 40;
    info.poll();
    shown.push_back (info.text());
    info.enter (LENGTH);
    shown.push_back (info.text());
    time.now += infoGraceMs * 2;   // the first leave's grace is gone with it
    info.poll();
    shown.push_back (info.text());

    CHECK (shown == std::vector<juce::String> { RATE, RATE, LENGTH, LENGTH });
}

TEST_CASE ("info: the pointer wins over the focus, and the focus is still true when it leaves")
{
    Time time;
    InfoState info (time.clock());

    info.focus (LENGTH);
    info.enter (RATE);
    CHECK (info.text() == RATE);

    info.leave();
    time.now += infoGraceMs;
    info.poll();
    CHECK (info.text() == LENGTH);

    info.blur();
    info.focus (RATE);             // Tab to the next control: blur, then focus, at once
    time.now += infoGraceMs;
    info.poll();
    CHECK (info.text() == RATE);
}

TEST_CASE ("info: a line that changes under the pointer is shown as it is now")
{
    Time time;
    InfoState info (time.clock());
    juce::Component pads, other;

    info.enter (RATE, &pads);
    info.refresh (&other, LENGTH);
    CHECK (info.text() == RATE);
    info.refresh (&pads, LENGTH);
    CHECK (info.text() == LENGTH);
}

/* ------------------------------------------------------------- tracker -- */

TEST_CASE ("info: the window hears the pointer over any component in it")
{
    Time time;
    Window window (time.clock());
    juce::Component knob, label;
    window.addAndMakeVisible (knob);
    knob.addAndMakeVisible (label);
    setInfo (knob, RATE);
    InfoTracker tracker (window, window.state);

    tracker.mouseEnter (eventFor (label));   // inside the knob's label: the knob's line
    CHECK (window.state.text() == RATE);

    /* While a button is held, the bar keeps the dragged control's line. */
    juce::Component elsewhere;
    window.addAndMakeVisible (elsewhere);
    tracker.mouseEnter (eventFor (elsewhere, juce::ModifierKeys::leftButtonModifier));
    CHECK (window.state.text() == RATE);

    tracker.mouseEnter (eventFor (elsewhere));   // no line there: the grace, then nothing
    time.now += infoGraceMs;
    window.state.poll();
    CHECK (window.state.text().isEmpty());
}

/* --------------------------------------------------------------- focus -- */

TEST_CASE ("focus: visible by Tab, hidden by a click, and the bar follows it")
{
    Time time;
    Window window (time.clock());
    Control knob;
    window.addAndMakeVisible (knob);
    setInfo (knob, RATE);

    knob.focus.focusGained (juce::Component::focusChangedByMouseClick);
    CHECK_FALSE (knob.focus.isVisible());
    CHECK_FALSE (isFocusVisible (knob));
    CHECK (window.state.text().isEmpty());

    knob.focus.focusGained (juce::Component::focusChangedByTabKey);
    CHECK (knob.focus.isVisible());
    CHECK (isFocusVisible (knob));
    CHECK (window.state.text() == RATE);

    knob.focus.pointerUsed();               // a press hides it again
    CHECK_FALSE (knob.focus.isVisible());
    time.now += infoGraceMs;
    window.state.poll();
    CHECK (window.state.text().isEmpty());
}

TEST_CASE ("focus: focus given by code shows once a key is used on it")
{
    Time time;
    Window window (time.clock());
    Control knob;
    window.addAndMakeVisible (knob);

    knob.focus.keyUsed();
    CHECK_FALSE (knob.focus.isVisible());   // no focus yet: nothing to show

    knob.focus.focusGained (juce::Component::focusChangedDirectly);
    CHECK_FALSE (knob.focus.isVisible());
    knob.focus.keyUsed();
    CHECK (knob.focus.isVisible());

    knob.focus.focusLost();
    CHECK_FALSE (knob.focus.isVisible());
}

TEST_CASE ("focus: a stock widget with no helper counts plain keyboard focus")
{
    juce::Component widget;
    CHECK_FALSE (isFocusVisible (widget));
}

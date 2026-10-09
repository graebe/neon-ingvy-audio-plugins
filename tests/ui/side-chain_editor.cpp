// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's native editor, held to its manual (plugins/side-chain/
 * docs/live.md) and to the web editor it replaces (plugins/side-chain/ui):
 *
 *   the parameters    the fifteen, in the iPlug2 order, with the plugin's text
 *   the layout        760 x 604, the as-built artboard; the source's own
 *                     controls and only those, nothing else moving
 *   the 1.1.0 changes Delay bipolar, Note and Channel stepped knobs, Mode and
 *                     Time switches
 *   the handles       each a parameter: a drag one gesture after a 4px dead
 *                     zone, Shift finer, its own stage only, the bottom corner
 *                     two parameters in one gesture, a double-click the
 *                     defaults, every key one edit
 *   the header        source, rate, stage, and the one amber warning in its
 *                     order, the trigger's only after most of a second
 *   the plot          its caption, playhead, landmark and overrun mark; the
 *                     audio on its level range, the shape and the envelope
 *                     on their own, a double-click on the well full scale
 *   the hint          the conventions, the times in the unit Time asks for
 *
 * and then its main states as pictures.
 */
#include "SideChainEditor.h"

#include "Pointer.h"
#include "checks.h"
#include "events.h"
#include "side-chain_fakes.h"
#include "snapshot.h"

#include "UvTokens.h"
#include "UvType.h"

#include <doctest.h>

using namespace ni::sc;
using ni::sc::test::FakeModel;
using ni::ui::gallery::key;
using ni::ui::gallery::Pointer;

namespace
{
/* An editor on a model and a clock the test owns. */
struct Rig
{
    double ms = 0.0;
    FakeModel model;
    SideChainEditor editor { model, [this] { return ms; } };

    Rig()
    {
        editor.frame().ground().setReducedMotionQuery ([] { return false; });
        model.params.clear();
    }

    /* One display frame, `dt` ms after the last. */
    void frame (double dt = 1000.0 / 60.0)
    {
        ms += dt;
        editor.tick (ms);
    }

    Shaper& shaper() { return editor.shaper(); }
    Shaper::Handle& handle (Shaper::Which w) { return shaper().handle (w); }

    /* A drag of a handle by (dx, dy) in the well's pixels, in one move. */
    void dragHandle (Shaper::Which w, float dx, float dy, juce::ModifierKeys mods = {})
    {
        auto& h = handle (w);
        const auto from = h.getBounds().toFloat().getCentre();
        Pointer p;
        p.down (h, h.getLocalPoint (&shaper(), from), mods);
        p.drag (h.getLocalPoint (&shaper(), from + juce::Point<float> (dx, dy)));
        p.up();
    }

    void setSource (Source s)
    {
        model.params[param::source].setValueNotifyingHost ((float) s / 2.0f);
        model.engine.source = s;
        model.params.clear();
    }

    juce::String conventions()
    {
        juce::StringArray out;
        for (const auto& c : editor.frame().hint().getConventions())
            out.add (c.name + " " + c.rest);
        return out.joinIntoString (" | ");
    }
};

/* The plot's whole width, 684px, is one cycle: 6.84px is 1 %. */
constexpr float pxPerPercent = 6.84f;
} // namespace

/* ------------------------------------------------------------ parameters -- */

TEST_CASE ("side-chain: the fifteen parameters, in the iPlug2 order, with the plugin's text")
{
    FakeModel model;
    REQUIRE (model.numParameters() == param::count);
    const char* names[] { "Source", "Rate", "Time", "Delay", "Attack", "Hold", "Release", "Depth",
                          "Curve", "Channel", "Trigger", "Mode", "Vel", "Threshold", "Lockout" };
    for (int i = 0; i < param::count; ++i)
    {
        CAPTURE (i);
        CHECK (model.parameter (i).getName (64) == names[i]);
    }
    /* The defaults the fixture's default.json decodes: 0 4 0 0 2 8 35 100 1 1 36 0 0 -24 20. */
    CHECK (model.parameter (param::rate).getCurrentValueAsText() == "1/4");
    CHECK (model.parameter (param::delay).getCurrentValueAsText() == "0.0 %");
    CHECK (model.parameter (param::release).getCurrentValueAsText() == "35.0 %");
    CHECK (model.parameter (param::curve).getCurrentValueAsText() == "Exponential");
    CHECK (model.parameter (param::channel).getCurrentValueAsText() == "1");
    CHECK (model.parameter (param::note).getCurrentValueAsText() == "C1");
    CHECK (model.parameter (param::threshold).getCurrentValueAsText() == "-24.0 dB");
    CHECK (model.parameter (param::lockout).getCurrentValueAsText() == "20 ms");
    CHECK (model.parameter (param::delay).getDefaultValue() == doctest::Approx (0.5f));
    CHECK (model.parameter (param::rate).getDefaultValue() == doctest::Approx (4.0f / 11.0f));
    CHECK (model.parameter (param::note).getDefaultValue() == doctest::Approx (36.0f / 127.0f));
    CHECK (model.parameter (param::threshold).getText (0.0f, 32) == "-inf dB");
}

/* ---------------------------------------------------------------- layout -- */

TEST_CASE ("side-chain: 760 x 604, the plot over the knobs over the two rows, inside the window padding")
{
    Rig rig;
    CHECK (rig.editor.getWidth() == SideChainEditor::designWidth);
    CHECK (rig.editor.getHeight() == SideChainEditor::designHeight);
    CHECK (rig.editor.content().getBounds() == juce::Rectangle<int> (32, 32, 696, 504));

    CHECK (rig.shaper().getBounds() == juce::Rectangle<int> (0, 24, 696, 260));
    /* On Cycle the five knobs share the row, 120px each: the artboard's cells. */
    CHECK (rig.editor.knob (param::depth).getBounds() == juce::Rectangle<int> (0, 308, 120, 106));
    CHECK (rig.editor.knob (param::delay).getX() == 144);
    CHECK (rig.editor.knob (param::release).getBounds() == juce::Rectangle<int> (576, 308, 120, 106));
    /* The rows at 464 and 500 in the window, as the web editor and the
     * artboard put them: on the 4px grid. */
    CHECK (rig.editor.select (param::source).getBounds().getPosition() == juce::Point<int> (0, 432));
    CHECK (rig.editor.select (param::curve).getBounds().getPosition() == juce::Point<int> (0, 468));
    CHECK ((32 + SideChainEditor::triggerRowY) % 4 == 0);
    CHECK ((32 + SideChainEditor::shapeRowY) % 4 == 0);
    /* The hint bar on the bottom edge, 16px above it. */
    CHECK (rig.editor.frame().hint().getBottom() == 604 - 16);
}

TEST_CASE ("side-chain: a fixed design, scaled to whatever the host gives")
{
    Rig rig;
    rig.editor.setSize (380, 302);
    CHECK (rig.editor.fit().getScale() == doctest::Approx (0.5f));
    /* Nothing reflows: the content is laid out at the design's size. */
    CHECK (rig.editor.frame().getWidth() == SideChainEditor::designWidth);
    CHECK (rig.editor.knob (param::release).getX() == 576);
}

TEST_CASE ("side-chain: each source shows its own controls and only those; the knobs share the row")
{
    Rig rig;
    auto& e = rig.editor;
    /* The shape's five come first, in their order, whatever the source. */
    const auto shapeFirst = [&]
    {
        int x = -1;
        for (const int i : { param::depth, param::delay, param::attack, param::hold, param::release })
        {
            if (e.knob (i).getX() <= x)
                return false;
            x = e.knob (i).getX();
        }
        return e.knob (param::depth).getX() == 0;
    };
    const auto curveRow = e.select (param::curve).getBounds();
    const auto sourceRow = e.select (param::source).getBounds();

    /* Cycle: the rate, nothing of MIDI's or the key's. */
    CHECK (e.select (param::rate).isVisible());
    for (const int i : { param::note, param::channel, param::velSens, param::threshold, param::lockout })
        CHECK_FALSE (e.knob (i).isVisible());
    CHECK_FALSE (e.toggle (param::midiMode).isVisible());

    /* MIDI: Note, Ch and Vel after the shape's five, eight knobs of 66px,
     * and the Gate switch. */
    rig.setSource (Source::midi);
    CHECK_FALSE (e.select (param::rate).isVisible());
    CHECK (e.knob (param::note).isVisible());
    CHECK (e.knob (param::depth).getWidth() == 66);
    CHECK (e.knob (param::note).getX() == 5 * 90);
    CHECK (e.knob (param::channel).getX() == 6 * 90);
    CHECK (e.knob (param::velSens).getX() == 7 * 90);
    CHECK (e.knob (param::velSens).getRight() == 696);
    CHECK (e.toggle (param::midiMode).isVisible());
    CHECK (e.toggle (param::midiMode).getX() == e.select (param::source).getRight() + 16);
    CHECK_FALSE (e.knob (param::threshold).isVisible());

    /* Sidechain: Thresh and Lockout behind the shape's five; seven knobs of
     * 78.9px, each edge rounded on its own, the last on the row's edge. */
    rig.setSource (Source::sidechain);
    CHECK (e.knob (param::threshold).isVisible());
    CHECK (e.knob (param::threshold).getX() == 514);
    CHECK (e.knob (param::lockout).isVisible());
    CHECK (e.knob (param::lockout).getRight() == 696);
    CHECK_FALSE (e.knob (param::note).isVisible());
    CHECK_FALSE (e.toggle (param::midiMode).isVisible());
    CHECK_FALSE (e.select (param::rate).isVisible());

    CHECK (shapeFirst());
    CHECK (e.select (param::curve).getBounds() == curveRow);
    CHECK (e.select (param::source).getBounds() == sourceRow);
}

/* -------------------------------------------------------- 1.1.0 controls -- */

TEST_CASE ("side-chain: Delay draws its arc from 12 o'clock; the others from the minimum")
{
    Rig rig;
    CHECK (rig.editor.knob (param::delay).isBipolar());
    for (const int i : { param::depth, param::attack, param::hold, param::release, param::velSens,
                         param::threshold, param::lockout, param::note, param::channel })
        CHECK_FALSE (rig.editor.knob (i).isBipolar());
}

TEST_CASE ("side-chain: Note and Ch are stepped knobs -- an arrow is one note, one channel")
{
    Rig rig;
    rig.setSource (Source::midi);
    auto& note = rig.editor.knob (param::note);
    auto& channel = rig.editor.knob (param::channel);
    CHECK (note.getValueText() == "C1");
    CHECK (channel.getValueText() == "1");

    CHECK (key (note.dial(), juce::KeyPress::upKey));
    CHECK (note.getValueText() == "C#1");
    CHECK (key (channel.dial(), juce::KeyPress::downKey));
    CHECK (channel.getValueText() == "Omni");
    CHECK (rig.model.params.log() == "begin 10, value 10 0.291, end 10, begin 9, value 9 0.000, end 9");
}

TEST_CASE ("side-chain: Mode and Time, two options each, are switches")
{
    Rig rig;
    rig.setSource (Source::midi);
    auto& gate = rig.editor.toggle (param::midiMode);
    auto& percent = rig.editor.toggle (param::timeMode);
    CHECK (gate.getLabel() == "Gate");
    CHECK (percent.getLabel() == "% of cycle");
    CHECK_FALSE (gate.isOn());

    Pointer p;
    p.click (gate, { 7.0f, 14.0f });
    CHECK (gate.isOn());
    CHECK (rig.model.parameter (param::midiMode).getCurrentValueAsText() == "Gate");

    p.click (percent, { 7.0f, 14.0f });
    CHECK (rig.model.parameter (param::timeMode).getCurrentValueAsText() == "% of cycle");
}

TEST_CASE ("side-chain: the knobs are the handles' parameters, as numbers")
{
    Rig rig;
    auto& release = rig.editor.knob (param::release);
    CHECK (release.getValueText() == "35.0 %");
    /* Typed text is the plugin's to read: a percentage of the cycle. */
    release.onText ("25");
    CHECK (rig.model.params.log() == "begin 6, value 6 0.125, end 6");
    CHECK (rig.shaper().getMarks().end == doctest::Approx (2.0 + 8.0 + 25.0));
}

/* --------------------------------------------------------------- handles -- */

TEST_CASE ("side-chain: the handles stand where the shape's stages begin and end")
{
    Rig rig;
    auto& s = rig.shaper();
    /* 0, 2, 10, 45 % of the cycle; the corners at the floor, 1 - Depth. */
    CHECK (s.handlePoint (Shaper::Which::start).x == doctest::Approx (6.0f));
    CHECK (s.handlePoint (Shaper::Which::bottom).x == doctest::Approx (6.0f + 2.0f * pxPerPercent));
    CHECK (s.handlePoint (Shaper::Which::holdEnd).x == doctest::Approx (6.0f + 10.0f * pxPerPercent));
    CHECK (s.handlePoint (Shaper::Which::end).x == doctest::Approx (6.0f + 45.0f * pxPerPercent));
    CHECK (s.handlePoint (Shaper::Which::start).y == doctest::Approx (s.top()));
    CHECK (s.handlePoint (Shaper::Which::bottom).y == doctest::Approx (s.mid()));
    CHECK (rig.handle (Shaper::Which::end).getBounds().getCentre()
           == s.handlePoint (Shaper::Which::end).roundToInt());
}

TEST_CASE ("side-chain: a drag is one gesture on its own stage; the handle follows at once")
{
    Rig rig;
    rig.dragHandle (Shaper::Which::end, 10.0f * pxPerPercent, 0.0f);
    CHECK (rig.model.params.log() == "begin 6, value 6 0.225, end 6");
    /* Release alone: the stages before it did not move. */
    CHECK (rig.model.plain (param::attack) == doctest::Approx (2.0));
    CHECK (rig.model.plain (param::hold) == doctest::Approx (8.0));
    CHECK (rig.shaper().handlePoint (Shaper::Which::end).x == doctest::Approx (6.0f + 55.0f * pxPerPercent));
}

TEST_CASE ("side-chain: a press inside the 4px dead zone is no edit")
{
    Rig rig;
    rig.dragHandle (Shaper::Which::holdEnd, 3.0f, -3.0f);
    CHECK (rig.model.params.log() == "");
}

TEST_CASE ("side-chain: Shift, chosen at the press, is five times finer")
{
    Rig rig;
    rig.dragHandle (Shaper::Which::holdEnd, 10.0f * pxPerPercent, 0.0f, juce::ModifierKeys::shiftModifier);
    CHECK (rig.model.plain (param::hold) == doctest::Approx (10.0).epsilon (0.001));
}

TEST_CASE ("side-chain: the bottom corner moves Attack sideways and Depth up and down, in one gesture")
{
    Rig rig;
    /* 1 % later, and up by a fifth of the half-height: 20 % shallower. */
    rig.dragHandle (Shaper::Which::bottom, pxPerPercent, -0.2f * rig.shaper().half());
    CHECK (rig.model.params.log()
           == "begin 4, begin 7, value 4 0.015, value 7 0.800, end 4, end 7");
    CHECK (rig.shaper().getMarks().floor == doctest::Approx (0.2));
}

TEST_CASE ("side-chain: Delay drags both ways; on Cycle an early duck wraps to the cycle's end")
{
    Rig rig;
    rig.dragHandle (Shaper::Which::start, -20.0f * pxPerPercent, 0.0f);
    CHECK (rig.model.plain (param::delay) == doctest::Approx (-20.0).epsilon (0.001));
    CHECK (rig.shaper().getMarks().start == doctest::Approx (80.0).epsilon (0.001));
    CHECK (rig.editor.knob (param::delay).getValueText() == "-20.0 %");

    /* MIDI has nothing periodic to anticipate: a negative Delay is no wait. */
    rig.setSource (Source::midi);
    CHECK (rig.shaper().getMarks().start == doctest::Approx (0.0));
}

TEST_CASE ("side-chain: a double-click on the bottom corner puts Attack (2 %) and Depth (100 %) back")
{
    Rig rig;
    rig.dragHandle (Shaper::Which::bottom, 5.0f * pxPerPercent, -0.5f * rig.shaper().half());
    rig.model.params.clear();

    auto& h = rig.handle (Shaper::Which::bottom);
    Pointer p;
    p.doubleClick (h, ni::ui::test::centreOf (h));
    CHECK (rig.model.params.log() == "begin 4, value 4 0.010, end 4, begin 7, value 7 1.000, end 7");
}

TEST_CASE ("side-chain: from the keyboard a handle is a slider -- every key one edit")
{
    Rig rig;
    auto& end = rig.handle (Shaper::Which::end);
    CHECK (key (end, juce::KeyPress::rightKey));
    CHECK (rig.model.params.log() == "begin 6, value 6 0.185, end 6");

    rig.model.params.clear();
    CHECK (key (end, juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier));
    CHECK (key (end, juce::KeyPress::pageDownKey));
    CHECK (rig.model.params.log()
           == "begin 6, value 6 0.187, end 6, begin 6, value 6 0.087, end 6");

    /* Up and Down are Depth's, on the bottom corner only: Up is shallower. */
    rig.model.params.clear();
    CHECK_FALSE (key (end, juce::KeyPress::upKey));
    auto& bottom = rig.handle (Shaper::Which::bottom);
    CHECK (key (bottom, juce::KeyPress::upKey));
    CHECK (rig.model.params.log() == "begin 7, value 7 0.990, end 7");

    /* Home and End: the ends of the stage's range. */
    rig.model.params.clear();
    CHECK (key (bottom, juce::KeyPress::homeKey));
    CHECK (rig.model.plain (param::attack) == doctest::Approx (0.0));
    CHECK (key (rig.handle (Shaper::Which::start), juce::KeyPress::endKey));
    CHECK (rig.model.plain (param::delay) == doctest::Approx (100.0));
}

TEST_CASE ("side-chain: a handle is a slider to a screen reader, named for what it moves")
{
    Rig rig;
    auto& bottom = rig.handle (Shaper::Which::bottom);
    CHECK (bottom.getTitle() == "Attack and depth");
    const auto handler = bottom.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::slider);
    CHECK (handler->getValueInterface()->getCurrentValueAsString() == "2.0 %, 100.0 %");
}

TEST_CASE ("side-chain: a handle reads and takes a stage in the unit its knob does")
{
    Rig rig;
    rig.model.engine.msPerCycle = 500.0;
    rig.frame();
    rig.model.params.clear();

    auto& end = rig.handle (Shaper::Which::end);
    const auto handler = end.createAccessibilityHandler();
    auto* value = handler->getValueInterface();
    REQUIRE (value != nullptr);

    /* In ms, as the Release knob says it. */
    CHECK (value->getCurrentValueAsString() == "175 ms");
    CHECK (value->getCurrentValueAsString() == rig.editor.knob (param::release).getValueText());

    /* A bare number is the unit shown: 40 ms of a 500 ms cycle is 8 %. */
    value->setValueAsString ("40");
    CHECK (rig.model.plain (param::release) == doctest::Approx (8.0));
    value->setValueAsString ("20 %");
    CHECK (rig.model.plain (param::release) == doctest::Approx (20.0));

    /* In % of the cycle, the knob's percentage, and a bare number is one. */
    rig.model.params[param::timeMode].setValueNotifyingHost (1.0f);
    CHECK (value->getCurrentValueAsString() == "20.0 %");
    value->setValueAsString ("40");
    CHECK (rig.model.plain (param::release) == doctest::Approx (40.0));

    /* Text it cannot read changes nothing. */
    rig.model.params.clear();
    value->setValueAsString ("nonsense");
    CHECK (rig.model.params.log() == "");

    /* The bottom corner: Attack as its knob reads it, then Depth. */
    rig.model.params[param::timeMode].setValueNotifyingHost (0.0f);
    const auto bottom = rig.handle (Shaper::Which::bottom).createAccessibilityHandler();
    CHECK (bottom->getValueInterface()->getCurrentValueAsString() == "10 ms, 100.0 %");
}

/* ------------------------------------------------------------------ plot -- */

TEST_CASE ("side-chain: a shape longer than one cycle is marked, not accommodated")
{
    Rig rig;
    CHECK_FALSE (rig.shaper().overruns());
    rig.model.params[param::release].setValueNotifyingHost (100.0f / 200.0f);
    CHECK (rig.shaper().overruns());
    /* The unwrapped length, so an early duck is no excuse. */
    rig.model.params[param::delay].setValueNotifyingHost (0.4f);   // -20 %
    CHECK (rig.shaper().overruns());
}

TEST_CASE ("side-chain: the caption names the picture and its span, or that nothing is arriving")
{
    Rig rig;
    CHECK (rig.shaper().getCaption() == "SHAPE   ONE CYCLE   INPUT IN GREY BEHIND");

    rig.model.engine.msPerCycle = 500.0;
    rig.model.params[param::timeMode].setValueNotifyingHost (1.0f);
    rig.model.fillScope (512, 512, 0.0f);
    rig.frame();
    CHECK (rig.shaper().isQuiet());
    CHECK (rig.shaper().getCaption() == "SHAPE   ONE CYCLE, 500 MS   NOTHING REACHING THE PLUGIN");

    rig.model.fillScope (512, 100, 0.5f);
    rig.frame();
    CHECK_FALSE (rig.shaper().isQuiet());
    CHECK (rig.shaper().getCaption() == "SHAPE   ONE CYCLE, 500 MS   INPUT IN GREY BEHIND");
}

TEST_CASE ("side-chain: while the stages read in ms, the caption carries their total")
{
    Rig rig;
    rig.model.engine.msPerCycle = 500.0;
    rig.frame();
    /* 0 + 10 + 40 + 175 ms: the web hint's "225 ms total". */
    CHECK (rig.shaper().getCaption() == "SHAPE   ONE CYCLE, 500 MS   STAGES 225 MS   INPUT IN GREY BEHIND");

    /* In % of the cycle the stages read as their knobs do, and the caption
     * drops the total at once, without waiting for a frame. */
    rig.model.params[param::timeMode].setValueNotifyingHost (1.0f);
    CHECK (rig.shaper().getCaption() == "SHAPE   ONE CYCLE, 500 MS   INPUT IN GREY BEHIND");
    rig.model.params[param::timeMode].setValueNotifyingHost (0.0f);
    CHECK (rig.shaper().getCaption().contains ("STAGES 225 MS"));

    /* Before a tempo nothing reads in ms. */
    CHECK (captionFor (true, 0.0) == "SHAPE   ONE CYCLE   INPUT IN GREY BEHIND");
    CHECK (captionFor (false, 500.0, -12.4) == "SHAPE   ONE CYCLE, 500 MS   STAGES -12 MS   NOTHING REACHING THE PLUGIN");
}

TEST_CASE ("side-chain: the playhead is the sweep -- a cycle wraps, a one-shot duck stops at its end")
{
    State s;
    s.sweep = 0.25;
    CHECK (playheadOf (s) == doctest::Approx (0.25));
    s.sweep = 1.25;
    CHECK (playheadOf (s) == doctest::Approx (0.25));
    s.source = Source::midi;
    CHECK (playheadOf (s) == doctest::Approx (1.0));

    Rig rig;
    rig.model.engine.sweep = 0.6;
    rig.frame();
    CHECK (rig.shaper().playhead() == doctest::Approx (0.6));
}

TEST_CASE ("side-chain: the ruler spans the cycle in ms, its landmark where the duck reaches its floor")
{
    Rig rig;
    rig.model.engine.msPerCycle = 500.0;
    rig.frame();
    const auto& ticks = rig.shaper().axis().ticks();
    REQUIRE_FALSE (ticks.empty());
    /* The bottom at 2 % of 500 ms. */
    CHECK (ticks.front().ms == doctest::Approx (10.0));
    CHECK (ticks.front().text.contains ("ms"));

    ShapeMarks m;
    m.bottom = 120.0;
    CHECK (landmarkMs (m, 500.0) == 0.0);
}

TEST_CASE ("side-chain: the audio zooms with the material and says its range; the handles stay where they are")
{
    Rig rig;
    rig.model.engine.msPerCycle = 500.0;
    rig.model.fillScope (512, 512, 0.8f);
    rig.frame();
    auto& s = rig.shaper();
    CHECK (s.hasLevelRange());
    CHECK (s.levelRange().db() == 0.0f);
    CHECK (s.levelRange().label() == "0 dB");
    const auto bottom = s.handlePoint (Shaper::Which::bottom);
    const auto end = s.handlePoint (Shaper::Which::end);

    /* A track at -24 dBFS: widened at once when it is loud, narrowed after
     * the window when it is quiet -- and the shape, a gain, never moves. */
    rig.model.fillScope (512, 512, 0.063f);
    for (int i = 0; i < 300; ++i)
        rig.frame();
    CHECK (s.levelRange().db() == -18.0f);
    CHECK (s.handlePoint (Shaper::Which::bottom) == bottom);
    CHECK (s.handlePoint (Shaper::Which::end) == end);

    rig.model.fillScope (512, 512, 1.0f);
    rig.frame();
    CHECK (s.levelRange().db() == 0.0f);
}

TEST_CASE ("side-chain: only what has been seen counts towards the range")
{
    Rig rig;
    rig.model.fillScope (512, 0, 0.8f);
    rig.frame();
    CHECK (rig.shaper().levelRange().db() == ni::ui::plot::LevelRange::floorDb);
}

TEST_CASE ("side-chain: a double-click on the well holds full scale; on a handle it resets the handle")
{
    Rig rig;
    rig.model.fillScope (512, 512, 0.063f);
    rig.frame();
    auto& s = rig.shaper();
    REQUIRE (s.levelRange().db() == -24.0f);

    /* Away from every handle: the middle of the release's open stretch. */
    Pointer p;
    p.doubleClick (s, { s.xOf (80.0), s.mid() + 0.8f * s.half() });
    CHECK (s.levelRange().isFixed());
    CHECK (s.levelRange().label() == "0 dB fixed");
    rig.frame();
    CHECK (s.levelRange().fullScale() == 1.0f);

    /* A handle's double-click is the handle's. */
    rig.model.params[param::release].setValueNotifyingHost (0.4f);
    p.doubleClick (rig.handle (Shaper::Which::end), { 16.0f, 16.0f });
    CHECK (rig.model.params[param::release].getValue() == doctest::Approx (rig.model.params[param::release].getDefaultValue()));
    CHECK (s.levelRange().isFixed());

    p.doubleClick (s, { s.xOf (80.0), s.mid() + 0.8f * s.half() });
    CHECK_FALSE (s.levelRange().isFixed());
    CHECK (s.levelRange().db() == -24.0f);
}

TEST_CASE ("side-chain: no input is any seen column above the floor; an empty capture is no verdict")
{
    Scope empty;
    CHECK (hasInput (empty));

    FakeModel model;
    model.fillScope (64, 64, 0.011f);
    CHECK_FALSE (hasInput (model.capture));
    model.fillScope (64, 64, 0.5f);
    CHECK (hasInput (model.capture));
    /* A column the sweep has not reached is not silence. */
    model.fillScope (64, 0, 0.5f);
    CHECK_FALSE (hasInput (model.capture));
}

/* ---------------------------------------------------------------- header -- */

TEST_CASE ("side-chain: the header says the source, its rate on Cycle, and the stage")
{
    Rig rig;
    rig.model.engine.advancing = true;
    rig.frame();
    CHECK (rig.editor.stateText() == juce::String::fromUTF8 ("Cycle 1/4 \xc2\xb7 idle"));

    rig.model.engine.rate = 8;
    rig.model.engine.stage = Stage::release;
    rig.frame();
    CHECK (rig.editor.stateText() == juce::String::fromUTF8 ("Cycle 1/16 \xc2\xb7 release"));

    rig.setSource (Source::midi);
    rig.model.engine.stage = Stage::hold;
    rig.frame();
    CHECK (rig.editor.stateText() == juce::String::fromUTF8 ("MIDI \xc2\xb7 hold"));
}

TEST_CASE ("side-chain: one amber mark, in its order: the key, the input, then the trigger")
{
    State s;
    Buses unpatched, patched { true };

    s.source = Source::sidechain;
    CHECK (warningFor (s, unpatched, false, true) == "no key routed");
    CHECK (warningFor (s, patched, false, true) == "no input");
    CHECK (warningFor (s, patched, true, true) == "no trigger");
    CHECK (warningFor (s, patched, true, false) == "");

    s.source = Source::midi;
    CHECK (warningFor (s, unpatched, true, true) == "no midi");

    s.source = Source::cycle;
    CHECK (warningFor (s, unpatched, true, true) == "transport stopped");
    s.advancing = true;
    CHECK (warningFor (s, unpatched, true, true) == "");
}

TEST_CASE ("side-chain: a missing trigger is reported only after most of a second, and a fire clears it")
{
    Rig rig;
    rig.setSource (Source::midi);
    rig.model.fillScope (512, 512, 0.5f);
    rig.frame();
    CHECK (rig.editor.warningText() == "");

    rig.frame (700.0);
    CHECK (rig.editor.warningText() == "");
    rig.frame (200.0);
    CHECK (rig.editor.warningText() == "no midi");

    rig.model.engine.fires += 1;
    rig.frame();
    CHECK (rig.editor.warningText() == "");

    /* The silent track first: it is the more basic fact. */
    rig.model.fillScope (512, 512, 0.0f);
    rig.frame (1000.0);
    CHECK (rig.editor.warningText() == "no input");
}

/* ------------------------------------------------------------------ hint -- */

TEST_CASE ("side-chain: the hint states the window's conventions, and nothing else, in every source")
{
    Rig rig;
    rig.frame();
    const juce::String all = "drag a handle to shape the duck | shift for fine | double-click to reset";
    CHECK (rig.conventions() == all);
    rig.setSource (Source::midi);
    rig.frame();
    CHECK (rig.conventions() == all);
    /* The bar has room for all three: none is cut with an ellipsis. */
    auto& bar = rig.editor.frame().hint();
    CHECK (ni::ui::clausesWidth (bar.getConventions()) <= (float) bar.tipsBounds().getWidth());
}

TEST_CASE ("side-chain: Time reads the stages in ms or in % of the cycle; the sound is the same")
{
    Rig rig;
    rig.model.engine.msPerCycle = 500.0;
    rig.model.params[param::delay].setValueNotifyingHost (0.45f);   // -10 %
    rig.frame();
    CHECK (rig.editor.knob (param::delay).getValueText() == "-50 ms");
    CHECK (rig.editor.knob (param::attack).getValueText() == "10 ms");
    CHECK (rig.editor.knob (param::release).getValueText() == "175 ms");
    /* Depth is no time. */
    CHECK (rig.editor.knob (param::depth).getValueText() == "100.0 %");

    rig.model.params[param::timeMode].setValueNotifyingHost (1.0f);
    CHECK (rig.editor.knob (param::attack).getValueText() == "2.0 %");
    CHECK (rig.editor.knob (param::release).getValueText() == "35.0 %");
    CHECK (rig.editor.knob (param::delay).getValueText() == "-10.0 %");

    /* Before a tempo the cycle has no length, and ms cannot be read. */
    Rig early;
    CHECK (early.editor.knob (param::attack).getValueText() == "2.0 %");
}

TEST_CASE ("side-chain: typing into a stage, the unit you type wins; a bare number is the unit shown")
{
    Rig rig;
    rig.model.engine.msPerCycle = 500.0;
    rig.frame();
    rig.model.params.clear();
    auto& release = rig.editor.knob (param::release);

    release.onText ("100 ms");       // 20 % of a 500 ms cycle
    CHECK (rig.model.plain (param::release) == doctest::Approx (20.0));
    release.onText ("50 %");
    CHECK (rig.model.plain (param::release) == doctest::Approx (50.0));
    release.onText ("250");          // shown in ms: 50 %
    CHECK (rig.model.plain (param::release) == doctest::Approx (50.0));
    release.onText ("125");
    CHECK (rig.model.plain (param::release) == doctest::Approx (25.0));

    rig.model.params[param::timeMode].setValueNotifyingHost (1.0f);
    release.onText ("40");           // shown in %
    CHECK (rig.model.plain (param::release) == doctest::Approx (40.0));

    rig.model.params.clear();
    release.onText ("nonsense");
    CHECK (rig.model.params.log() == "");
}

TEST_CASE ("side-chain: every line is held to 72 characters, in every source")
{
    Rig rig;
    NI_CHECK_INFO_LIMIT (rig.editor);
    rig.setSource (Source::midi);
    NI_CHECK_INFO_LIMIT (rig.editor);
    rig.setSource (Source::sidechain);
    NI_CHECK_INFO_LIMIT (rig.editor);
    CHECK (ni::ui::infoOf (rig.handle (Shaper::Which::end)).startsWith ("Release"));
    CHECK (ni::ui::infoOf (rig.editor.knob (param::delay)).startsWith ("Delay"));
}

TEST_CASE ("side-chain: the controls are named in words, as the manual names them, and each line by its label")
{
    Rig rig;
    auto& e = rig.editor;
    CHECK (e.select (param::source).getTitle() == "Source");
    CHECK (e.knob (param::channel).getLabel() == "Channel");
    CHECK (e.knob (param::velSens).getLabel() == "Velocity");
    CHECK (e.knob (param::threshold).getLabel() == "Threshold");

    /* Each line starts with the label it explains. */
    for (const int i : { param::channel, param::velSens, param::threshold })
    {
        CAPTURE (i);
        CHECK (ni::ui::infoOf (e.knob (i)).startsWith (e.knob (i).getLabel() + juce::String::fromUTF8 (" \xe2\x80\x94")));
    }
    CHECK (ni::ui::infoOf (e.select (param::source)).startsWith ("Source"));

    /* The longest, THRESHOLD, fits its card at seven knobs, and SOURCE its
     * label's width: neither runs into the next thing. */
    rig.setSource (Source::sidechain);
    const auto& style = uv::tok::type::label;
    const auto font = uv::type::font (style);
    CHECK (uv::type::width (font, uv::type::cased (style, "Threshold")) <= (float) e.knob (param::threshold).getWidth());
    rig.setSource (Source::midi);
    CHECK (uv::type::width (font, uv::type::cased (style, "Velocity")) <= (float) e.knob (param::velSens).getWidth());
    CHECK (uv::type::width (font, uv::type::cased (style, "Source")) + 2.0f
           <= (float) (e.select (param::source).idealWidth() - 128 - 8));
}

TEST_CASE ("side-chain: each source's catch has its remedy where the control is explained")
{
    Rig rig;
    /* Live routes no MIDI to an audio track; a key is routed in Live. */
    CHECK (ni::ui::infoOf (rig.editor.select (param::source)).contains ("MIDI needs a MIDI track"));
    CHECK (ni::ui::infoOf (rig.editor.knob (param::threshold)).contains ("route it in Live's sidechain"));
}

TEST_CASE ("side-chain: no handle's line repeats the conventions' double-click")
{
    Rig rig;
    for (const auto w : { Shaper::Which::start, Shaper::Which::bottom, Shaper::Which::holdEnd, Shaper::Which::end })
        CHECK_FALSE (ni::ui::infoOf (rig.handle (w)).contains ("double-click"));
    CHECK (rig.conventions().contains ("double-click to reset"));
}

TEST_CASE ("side-chain: the Motion switch is the model's")
{
    Rig rig;
    CHECK (rig.editor.frame().motionSwitch().isOn());
    rig.model.common.motionOn = false;
    rig.editor.frame().motionChanged();
    CHECK_FALSE (rig.editor.frame().motionSwitch().isOn());
}

/* ------------------------------------------------------------- pictures -- */

namespace
{
/* A cycle at 120 bpm and 1/4 with a bass under it at `level`, the sweep part
 * way. */
void playing (Rig& rig, float level = 0.8f)
{
    rig.model.engine.msPerCycle = 500.0;
    rig.model.engine.sweep = 0.62;
    rig.model.engine.advancing = true;
    rig.model.engine.stage = Stage::release;
    rig.model.fillScope (512, 318, level);
    rig.frame();
}
} // namespace

NI_SNAPSHOT_TEST ("side-chain: Cycle, playing")
{
    Rig rig;
    playing (rig);
    NI_CHECK_SNAPSHOT (rig.editor, "side-chain-cycle");
}

NI_SNAPSHOT_TEST ("side-chain: Cycle, a quiet track at -24 dBFS, zoomed to fill the well")
{
    Rig rig;
    playing (rig, 0.063f);
    REQUIRE (rig.shaper().levelRange().db() == -24.0f);
    NI_CHECK_SNAPSHOT (rig.editor, "side-chain-quiet");
}

NI_SNAPSHOT_TEST ("side-chain: Cycle, a loud track at 0 dBFS, on full scale")
{
    Rig rig;
    playing (rig, 1.0f);
    REQUIRE (rig.shaper().levelRange().db() == 0.0f);
    NI_CHECK_SNAPSHOT (rig.editor, "side-chain-loud");
}

NI_SNAPSHOT_TEST ("side-chain: MIDI, no note arriving")
{
    Rig rig;
    rig.setSource (Source::midi);
    playing (rig);
    rig.model.engine.advancing = false;
    rig.model.engine.stage = Stage::idle;
    rig.frame (1000.0);
    NI_CHECK_SNAPSHOT (rig.editor, "side-chain-midi");
}

NI_SNAPSHOT_TEST ("side-chain: Sidechain, no key routed, nothing coming in, the shape overrunning")
{
    Rig rig;
    rig.setSource (Source::sidechain);
    rig.model.params[param::release].setValueNotifyingHost (0.6f);  // 120 %
    rig.model.engine.msPerCycle = 500.0;
    rig.model.fillScope (512, 512, 0.0f);
    rig.frame();
    NI_CHECK_SNAPSHOT (rig.editor, "side-chain-sidechain");
}

NI_SNAPSHOT_TEST ("side-chain: an early duck wrapping round the cycle, a handle with keyboard focus")
{
    Rig rig;
    rig.model.params[param::delay].setValueNotifyingHost (0.45f);   // -10 %: from 90 %
    rig.model.params[param::timeMode].setValueNotifyingHost (1.0f);
    playing (rig);
    auto& h = rig.handle (Shaper::Which::bottom);
    h.focusGained (juce::Component::focusChangedByTabKey);
    rig.editor.frame().infoState().focus (ni::ui::infoOf (h), &h);
    rig.frame();
    NI_CHECK_SNAPSHOT (rig.editor, "side-chain-focus");
}

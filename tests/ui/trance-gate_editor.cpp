// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate's native editor, held to its manual (plugins/trance-gate/
 * README.md, docs/live.md) and to the web editor it replaces
 * (plugins/trance-gate/ui):
 *
 *   the parameters    the fifteen, in the iPlug2 order, with the plugin's text
 *   the layout        the as-built artboard: 824 wide, the pads 644 down, a
 *                     row more of window per sixteen steps
 *   the controls      every knob, switch and select on its parameter, Rate
 *                     and Length stepped, Length holding on whole bars
 *   the stages        read in the unit Env Time asks for, typed in either
 *   the ring          a press, a sweep, and the keyboard's Length
 *   the hint          the conventions, Set order's, and an outcome's
 *
 * The pads' and the fade's behaviour is trance-gate_steps.cpp's, the window
 * verbs' trance-gate_verbs.cpp's, the plots' trance-gate_plots.cpp's.
 */
#include "TranceGateEditor.h"

#include "InfoLines.h"
#include "Pointer.h"
#include "checks.h"
#include "trance-gate_fakes.h"

#include <doctest.h>

#include <cmath>

using namespace ni::tg;
using namespace ni::tg::test;
using ni::ui::gallery::key;
using ni::ui::gallery::Pointer;

namespace
{
/* An editor on a model, a clipboard, panels and a clock the test owns. */
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
        model.params.clear();
        model.edits.clear();
    }

    /* The engine's next block, then one display frame. */
    void frame (double dt = 1000.0 / 60.0)
    {
        model.publish();
        ms += dt;
        editor.tick (ms);
    }

    juce::String conventions()
    {
        juce::StringArray out;
        for (const auto& c : editor.frame().hint().getConventions())
            out.add (c.name + " " + c.rest);
        return out.joinIntoString (" | ");
    }

    /* A point on wedge `i` of the ring, mid-band. */
    juce::Point<float> wedge (int i)
    {
        auto& ring = editor.ring();
        const auto b = ring.band();
        const float a = juce::MathConstants<float>::twoPi * ((float) i + 0.5f) / (float) ring.getCount();
        const float r = (b.inner + b.outer) * 0.5f;
        return { b.centre + std::sin (a) * r, b.centre - std::cos (a) * r };
    }
};
} // namespace

/* ------------------------------------------------------------ parameters -- */

TEST_CASE ("trance-gate: the fifteen parameters, in the iPlug2 order, with the plugin's text")
{
    FakeModel model;
    REQUIRE (model.numParameters() == param::count);
    const char* names[] { "Slot", "Length", "Rate", "Join Neighbors", "Env Time", "Env Curve", "Amount",
                          "Width", "Attack", "Decay", "Sustain", "Release", "Fade", "Fade Shape", "Fade Dir" };
    /* What a host reads for each at its default: the fixture's default.json. */
    const char* shown[] { "1", "16", "1/16", "Off", "ms", "Linear", "100.00 %", "100.00 %", "1.60 %",
                          "16.00 %", "100.00 %", "16.00 %", "100.00 %", "Hard", "In" };
    for (int i = 0; i < param::count; ++i)
    {
        CAPTURE (i);
        CHECK (model.parameter (i).getName (64) == names[i]);
        CHECK (model.parameter (i).getCurrentValueAsText() == shown[i]);
    }
    /* parameters.json's defaultNormalizedValue for the ones that are not 0 or 1. */
    CHECK (model.parameter (param::length).getDefaultValue() == doctest::Approx (15.0 / 127.0));
    CHECK (model.parameter (param::rate).getDefaultValue() == doctest::Approx (7.0 / 12.0));
    CHECK (model.parameter (param::attack).getDefaultValue() == doctest::Approx (0.008));
    CHECK (model.parameter (param::decay).getDefaultValue() == doctest::Approx (0.08));
    CHECK (model.parameter (param::release).getDefaultValue() == doctest::Approx (0.08));
    /* Their steps: stepCount + 1 values each. */
    CHECK (model.parameter (param::slot).getNumSteps() == 8);
    CHECK (model.parameter (param::length).getNumSteps() == 128);
    CHECK (model.parameter (param::rate).getNumSteps() == 13);
    CHECK (model.parameter (param::curve).getNumSteps() == 3);
}

/* ---------------------------------------------------------------- layout -- */

TEST_CASE ("trance-gate: the as-built window -- the ring and its column, three panels, a row, the band, the pads")
{
    Rig rig;
    auto& e = rig.editor;
    CHECK (e.getWidth() == 824);
    CHECK (e.getHeight() == 752);
    CHECK (TranceGateEditor::designHeightFor (16) == 752);
    CHECK (e.content().getBounds() == juce::Rectangle<int> (32, 32, 760, 652));

    CHECK (e.ring().getBounds() == juce::Rectangle<int> (0, 0, 240, 240));
    CHECK (e.envelopePlot().getBounds() == juce::Rectangle<int> (0, 264, 240, 104));
    /* The window verbs flush with the panels' bottom edge, where the web
     * editor's file verbs were. */
    CHECK (e.verbs().group().getBounds() == juce::Rectangle<int> (0, 424, 163, 28));
    CHECK (e.panel (0).getBounds() == juce::Rectangle<int> (272, 0, 488, 140));
    CHECK (e.panel (1).getBounds() == juce::Rectangle<int> (272, 156, 488, 140));
    CHECK (e.panel (2).getBounds() == juce::Rectangle<int> (272, 312, 488, 140));
    CHECK (e.select (param::slot).getBounds().getPosition() == juce::Point<int> (0, 468));
    CHECK (e.band().getBounds() == juce::Rectangle<int> (0, 504, 760, 92));
    /* The pads 644 below the window's top edge, as App.jsx's ABOVE_GRID. */
    CHECK (e.pads().getBounds() == juce::Rectangle<int> (0, 612, 760, 40));
    CHECK (e.content().getY() + e.pads().getY() == 644);
    CHECK (e.frame().hint().getBottom() == 752 - 16);
}

TEST_CASE ("trance-gate: the window grows a row at a time with Length, at the scale it has")
{
    Rig rig;
    auto& e = rig.editor;
    rig.model.next.length = 17;
    rig.frame();
    CHECK (e.getHeight() == 752 + 48);
    CHECK (e.pads().getHeight() == 88);
    CHECK (e.frame().hint().getBottom() == 800 - 16);

    rig.model.next.length = 128;
    rig.frame();
    CHECK (e.getHeight() == 1088);
    CHECK (e.pads().grid().getCount() == 128);

    /* Scaled by the host, it keeps its scale when it grows. */
    e.setSize (412, 544);
    CHECK (e.fit().getScale() == doctest::Approx (0.5f));
    rig.model.next.length = 16;
    rig.frame();
    CHECK (e.getWidth() == 412);
    CHECK (e.getHeight() == 376);
    CHECK (e.frame().getHeight() == 752);
}

TEST_CASE ("trance-gate: every knob the same size, the Fade panel's switches and actions at the full 28px")
{
    Rig rig;
    auto& e = rig.editor;
    const int w = e.knob (param::rate).getWidth();
    CHECK (w >= ni::ui::Knob::minWidth);
    for (const int i : { param::length, param::amount, param::width, param::attack, param::decay,
                         param::sustain, param::release, param::fade })
    {
        CAPTURE (i);
        CHECK (std::abs (e.knob (i).getWidth() - w) <= 1);
        CHECK (e.knob (i).getHeight() == ni::ui::Knob::cardHeight);
    }
    CHECK (e.knob (param::fade).getX() == e.knob (param::rate).getX());
    CHECK (e.knob (param::width).getRight() == e.panel (0).contentBounds().getRight());

    /* D2: two columns, nothing squeezed. */
    auto& out = e.toggle (param::fadeDir);
    auto& soft = e.toggle (param::fadeSoft);
    for (auto* c : std::initializer_list<juce::Component*> { &out, &soft, &e.orderButton(), &e.shuffleButton() })
        CHECK (c->getHeight() == 28);
    CHECK (out.getX() == soft.getX());
    CHECK (soft.getY() == out.getBottom() + 8);
    CHECK (e.orderButton().getX() == out.getRight() + 16);
    CHECK (e.shuffleButton().getY() == e.orderButton().getBottom() + 8);
    CHECK (e.orderButton().getRight() <= e.panel (2).contentBounds().getRight());
    /* As wide as the longest it can say, so counting never moves it. */
    CHECK (e.orderButton().getWidth() >= ni::ui::Button ("Set order 128/128").idealWidth());
}

TEST_CASE ("trance-gate: the settings row holds the four settings, space-4 apart, inside the content")
{
    Rig rig;
    auto& e = rig.editor;
    auto& slot = e.select (param::slot);
    auto& join = e.toggle (param::legato);
    auto& curve = e.select (param::curve);
    auto& time = e.toggle (param::timeMode);
    CHECK (slot.getX() == 0);
    CHECK (slot.field().getWidth() == 96);
    CHECK (join.getX() == slot.getRight() + 16);
    CHECK (curve.getX() == join.getRight() + 16);
    CHECK (time.getX() == curve.getRight() + 16);
    CHECK (time.getRight() <= 760);
    for (auto* c : std::initializer_list<juce::Component*> { &slot, &join, &curve, &time })
        CHECK (c->getY() == 468);

    CHECK (slot.getOptions() == juce::StringArray { "1", "2", "3", "4", "5", "6", "7", "8" });
    CHECK (curve.getOptions() == juce::StringArray { "Linear", "Exponential", "S-Curve" });
    CHECK (join.getLabel() == "Join Neighbors");
    CHECK (time.getLabel() == "Time in %");
    CHECK (e.toggle (param::fadeDir).getLabel() == "Out");
    CHECK (e.toggle (param::fadeSoft).getLabel() == "Soft");
}

/* -------------------------------------------------------------- controls -- */

TEST_CASE ("trance-gate: the switches are their parameters -- Out is Fade Dir's second option, Time in % Env Time's")
{
    Rig rig;
    auto& out = rig.editor.toggle (param::fadeDir);
    CHECK_FALSE (out.isOn());
    rig.model.set (param::fadeDir, 1.0f);
    CHECK (out.isOn());
    CHECK (rig.model.parameter (param::fadeDir).getCurrentValueAsText() == "Out");

    auto& time = rig.editor.toggle (param::timeMode);
    Pointer p;
    p.click (time, { 6.0f, 14.0f });
    CHECK (rig.model.parameter (param::timeMode).getCurrentValueAsText() == "%");
    CHECK (rig.model.params.log() == "begin 4, value 4 1.000, end 4");
}

TEST_CASE ("trance-gate: Rate and Length step -- an arrow is one division, one step")
{
    Rig rig;
    auto& rate = rig.editor.knob (param::rate);
    CHECK (rate.getValueText() == "1/16");
    CHECK (key (rate.dial(), juce::KeyPress::upKey));
    CHECK (rig.model.parameter (param::rate).getCurrentValueAsText() == "1/16T");
    CHECK (key (rate.dial(), juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier));
    CHECK (rig.model.parameter (param::rate).getCurrentValueAsText() == "1/16");

    auto& length = rig.editor.knob (param::length);
    CHECK (key (length.dial(), juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier));
    CHECK (rig.model.plain (param::length) == doctest::Approx (17.0f));
}

TEST_CASE ("trance-gate: Length holds on half a bar to four bars, at the Rate and the host's meter")
{
    Rig rig;
    auto& length = rig.editor.knob (param::length);
    const auto steps = [&]
    {
        rig.frame();
        juce::StringArray out;
        for (const auto d : length.getDetents())
            out.add (juce::String (juce::roundToInt (d * 127.0) + 1));
        return out.joinIntoString (",");
    };
    /* The manual's examples (docs/live.md, Length holds on whole bars). */
    CHECK (steps() == "8,16,32,64");
    rig.model.set (param::rate, 9.0f);   // 1/32
    CHECK (steps() == "16,32,64,128");
    rig.model.set (param::rate, 8.0f);   // 1/16T
    CHECK (steps() == "12,24,48,96");
    rig.model.set (param::rate, 7.0f);
    rig.model.meterNum = 3;
    CHECK (steps() == "6,12,24,48");

    /* Page Up goes to the next one, from the knob and from the ring alike. */
    rig.model.meterNum = 4;
    rig.frame();
    CHECK (key (length.dial(), juce::KeyPress::pageUpKey));
    CHECK (rig.model.plain (param::length) == doctest::Approx (32.0f));
    rig.model.next.length = 32;
    rig.frame();
    CHECK (key (rig.editor.ring(), juce::KeyPress::pageUpKey));
    CHECK (rig.model.plain (param::length) == doctest::Approx (64.0f));
    CHECK (key (rig.editor.ring(), juce::KeyPress::pageDownKey));
    CHECK (rig.model.plain (param::length) == doctest::Approx (16.0f));
}

TEST_CASE ("trance-gate: to the keyboard the ring is Length -- arrows by one, Home and End the ends")
{
    Rig rig;
    auto& ring = rig.editor.ring();
    CHECK (ring.getTitle() == "Length");
    CHECK (key (ring, juce::KeyPress::upKey));
    CHECK (rig.model.plain (param::length) == doctest::Approx (17.0f));
    CHECK (rig.model.params.log() == "begin 1, value 1 0.126, end 1");
    CHECK (key (ring, juce::KeyPress::endKey));
    CHECK (rig.model.plain (param::length) == doctest::Approx (128.0f));
    CHECK (key (ring, juce::KeyPress::homeKey));
    CHECK (rig.model.plain (param::length) == doctest::Approx (1.0f));
    /* No edit of the pattern itself from the ring's keys. */
    CHECK (rig.model.takeEdits().empty());
}

TEST_CASE ("trance-gate: the ring's centre is the pattern's length, its wedges what sounds")
{
    Rig rig;
    rig.model.next.steps[1] = StepMode::tie;
    rig.model.next.depths[2] = 0.5f;
    rig.frame();
    auto& ring = rig.editor.ring();
    CHECK (ring.getCentreValue() == "16");
    CHECK (ring.getStep (0).on);
    CHECK (ring.getStep (1).tie);
    CHECK (ring.getStep (2).amount == doctest::Approx (0.5f));
    CHECK_FALSE (ring.getStep (3).on);

    /* A step the fade has not brought in is the rail, as on its pad: at 50 %
     * four of the eight hits have arrived. */
    rig.model.next.steps[1] = StepMode::off;
    rig.model.set (param::fade, 50.0f);
    rig.frame();
    int lit = 0;
    for (int i = 0; i < 16; ++i)
        lit += ring.getStep (i).on || ring.getStep (i).tie ? 1 : 0;
    CHECK (lit == 4);
}

TEST_CASE ("trance-gate: a press on a wedge toggles its step; a sweep paints the same state across the ring")
{
    Rig rig;
    auto& ring = rig.editor.ring();
    Pointer p;
    p.down (ring, rig.wedge (1));
    CHECK (rig.model.takeEdits() == "step 1 on, depth 1 1.00");
    p.drag (rig.wedge (2));
    p.drag (rig.wedge (3));
    p.drag (rig.wedge (2));
    p.up();
    /* Step 2 was on and is painted on, without its amount touched; step 3
     * was off and comes on at its full amount. Back over 2, nothing. */
    CHECK (rig.model.takeEdits() == "step 2 on, step 3 on, depth 3 1.00");

    /* Shift is a tie, painted too. */
    p.down (ring, rig.wedge (4), juce::ModifierKeys::shiftModifier);
    p.drag (rig.wedge (5));
    p.up();
    CHECK (rig.model.takeEdits() == "step 4 tie, step 5 tie, depth 5 1.00");
}

/* ---------------------------------------------------------------- stages -- */

TEST_CASE ("trance-gate: the stages read in ms or in % of the gate, as Env Time asks; the sound is the same")
{
    Rig rig;
    rig.model.engineTransport.msPerStep = 125.0;
    rig.frame();
    auto& attack = rig.editor.knob (param::attack);
    auto& sustain = rig.editor.knob (param::sustain);
    CHECK (attack.getValueText() == "2.0 ms");
    CHECK (rig.editor.knob (param::decay).getValueText() == "20.0 ms");
    CHECK (sustain.getValueText() == "100.00 %");

    /* Width halves the gate, and the ms with it, the percentages staying. */
    rig.model.set (param::width, 50.0f);
    rig.frame();
    CHECK (attack.getValueText() == "1.0 ms");

    rig.model.set (param::timeMode, 1.0f);
    CHECK (attack.getValueText() == "1.60 %");
    CHECK (rig.model.params.events.empty());
}

TEST_CASE ("trance-gate: typing a stage, the unit typed wins; a bare number is in the unit shown")
{
    Rig rig;
    rig.model.engineTransport.msPerStep = 125.0;
    rig.frame();
    auto& attack = rig.editor.knob (param::attack);
    /* Time on ms, 40 is 40 ms of a 125 ms gate: 32 %. */
    attack.onText ("40");
    CHECK (rig.model.plain (param::attack) == doctest::Approx (32.0f).epsilon (0.001));
    attack.onText ("25 %");
    CHECK (rig.model.plain (param::attack) == doctest::Approx (25.0f).epsilon (0.001));
    rig.model.set (param::timeMode, 1.0f);
    attack.onText ("40");
    CHECK (rig.model.plain (param::attack) == doctest::Approx (40.0f).epsilon (0.001));
    attack.onText ("50 ms");
    CHECK (rig.model.plain (param::attack) == doctest::Approx (40.0f).epsilon (0.001));
    /* Text that is no number changes nothing. */
    rig.model.params.clear();
    attack.onText ("fast");
    CHECK (rig.model.params.events.empty());
}

/* ----------------------------------------------------------------- hint -- */

TEST_CASE ("trance-gate: the hint states the Step card's conventions, and Set order swaps in its own")
{
    Rig rig;
    CHECK (rig.conventions() == juce::String::fromUTF8 (
               "click a step to toggle | shift-click for a tie | drag up or down for its amount"));
    rig.editor.orderButton().onClick();
    CHECK (rig.conventions() == "click the steps in arrival order | a number to type one"
                                " | Set order again to finish");
    /* Under Fade Out the arrivals are the gaps. */
    rig.model.set (param::fadeDir, 1.0f);
    rig.frame();
    CHECK (rig.conventions().startsWith ("click the gaps in arrival order"));
    rig.editor.orderButton().onClick();
    CHECK (rig.conventions().startsWith ("click a step to toggle"));
}

TEST_CASE ("trance-gate: every set of conventions fits the bar whole, before the Motion switch")
{
    Rig rig;
    const float room = (float) rig.editor.frame().hint().tipsBounds().getWidth();
    CHECK (ni::ui::clausesWidth (info::conventions()) <= room);
    CHECK (ni::ui::clausesWidth (info::orderConventions (false)) <= room);
    CHECK (ni::ui::clausesWidth (info::orderConventions (true)) <= room);
}

TEST_CASE ("trance-gate: an outcome takes the bar's first place for six seconds; Set order clears it")
{
    Rig rig;
    rig.editor.verbs().copy();
    auto shown = rig.editor.frame().outcome();
    REQUIRE (shown.has_value());
    CHECK (shown->name == "Copied");
    CHECK (shown->rest == "slot 1.");
    rig.ms += 5999.0;
    CHECK (rig.editor.frame().outcome().has_value());
    rig.ms += 2.0;
    CHECK_FALSE (rig.editor.frame().outcome().has_value());

    rig.editor.verbs().paste();
    CHECK (rig.editor.frame().outcome().has_value());
    rig.editor.orderButton().onClick();
    CHECK_FALSE (rig.editor.frame().outcome().has_value());
}

TEST_CASE ("trance-gate: every control says what it does, and every line holds to 72 characters, in every state")
{
    Rig rig;
    auto& e = rig.editor;
    NI_CHECK_INFO_LIMIT (e);
    CHECK (ni::ui::infoOf (e.knob (param::rate)) == info::rate.str());
    CHECK (ni::ui::infoOf (e.ring()) == info::ring.str());
    CHECK (ni::ui::infoOf (e.ring().centre()) == info::steps.str());
    CHECK (ni::ui::infoOf (e.envelopePlot()) == info::envelopePlot.str());
    CHECK (ni::ui::infoOf (e.band().pattern()) == info::patternPlot.str());
    CHECK (ni::ui::infoOf (e.band().signal()) == info::signalPlot.str());
    CHECK (ni::ui::infoOf (e.pads().grid()) == info::pads.str());
    CHECK (ni::ui::infoOf (e.orderButton()) == info::order.str());
    CHECK (ni::ui::infoOf (e.panel (2).titleComponent()) == info::fadePanel.str());

    /* Set order, with the fade part way in: the pads' line changes, and the
     * arrival numbers show theirs. */
    rig.model.set (param::fade, 50.0f);
    e.orderButton().onClick();
    rig.frame();
    CHECK (ni::ui::infoOf (e.pads().grid()) == info::padsOrder.str());
    REQUIRE (e.pads().arrival (0) != nullptr);
    CHECK (ni::ui::infoOf (*e.pads().arrival (0)) == info::arrival.str());
    NI_CHECK_INFO_LIMIT (e);
    for (const auto& c : info::orderConventions (true))
        CHECK ((c.name + " " + c.rest).length() <= ni::ui::infoLimit);
}

TEST_CASE ("trance-gate: the playhead is the model's transport, on the ring, the pads and the plot alike")
{
    Rig rig;
    rig.model.engineTransport = { true, 5.4, 125.0 };
    rig.frame();
    CHECK (rig.editor.ring().getPlayhead() == 5);
    CHECK (rig.editor.ring().isPlaying());
    CHECK (rig.editor.pads().grid().getState (5).play);
    CHECK_FALSE (rig.editor.pads().grid().getState (4).play);
    CHECK (rig.editor.band().pattern().getPhase() == doctest::Approx (5.4));

    /* Past the pattern's end it wraps; stopped, there is none. */
    rig.model.engineTransport.phase = 18.25;
    rig.frame();
    CHECK (rig.editor.ring().getPlayhead() == 2);
    rig.model.engineTransport.playing = false;
    rig.frame();
    CHECK_FALSE (rig.editor.ring().isPlaying());
    CHECK_FALSE (rig.editor.pads().grid().getState (2).play);
    CHECK (rig.editor.band().pattern().getPhase() < 0.0);
}

TEST_CASE ("trance-gate: the tabs show one plot at a time, from the pointer or the keys")
{
    Rig rig;
    auto& band = rig.editor.band();
    CHECK (band.pattern().isVisible());
    CHECK_FALSE (band.signal().isVisible());
    band.tabs().onSelect (1);
    CHECK (band.shown() == 1);
    CHECK (band.signal().isVisible());
    CHECK_FALSE (band.pattern().isVisible());
    CHECK (band.tabs().getBounds() == juce::Rectangle<int> (736, 0, 24, 92));
}

TEST_CASE ("trance-gate: the Motion switch is the model's, and Randomize is the engine's")
{
    Rig rig;
    rig.model.common.motionOn = false;
    rig.editor.frame().motionChanged();
    CHECK_FALSE (rig.editor.frame().motionSwitch().isOn());
    rig.editor.verbs().button (Verbs::Verb::randomize).onClick();
    CHECK (rig.model.takeEdits() == "randomize");
}

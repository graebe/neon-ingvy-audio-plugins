// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Filter Designer's editor against a model with nothing behind it
 * (filter-designer_fakes.h): the window and its grid, what each gesture on
 * the picture asks of the host -- a drag one gesture per parameter, Alt for
 * Q, an edge tick for the width, a double-click to add or reset, Delete, the
 * wheel, the caption's two clicks -- the row's cells, the inspector bound to
 * the selection and lit as its type uses, every line the window writes, and
 * the window as a picture.
 *
 * What only the real model can show -- the curves and the handles being the
 * engine's -- is tests/filter-designer/fd_processor.cpp's.
 */
#include "Editor.h"
#include "InfoLines.h"
#include "Pointer.h"
#include "checks.h"
#include "filter-designer_fakes.h"
#include "snapshot.h"

#include <doctest.h>

using namespace ni::fd;
using ni::fd::test::FakeModel;
using ni::ui::gallery::Pointer;

namespace
{
struct Rig
{
    double ms = 0.0;
    FakeModel model;
    Editor editor { model, [this] { return ms; } };

    Rig()
    {
        editor.frame().ground().setReducedMotionQuery ([] { return false; });
        model.params.clear();
    }

    void frames (int n = 1)
    {
        for (int i = 0; i < n; ++i)
        {
            ms += 1000.0 / 60.0;
            editor.tick (ms);
        }
    }

    double plain (int i) { return model.plain (i); }

    void set (int i, double v)
    {
        auto& p = model.parameter (i);
        p.setValueNotifyingHost (p.convertTo0to1 ((float) v));
    }

    /* Where a frequency and a level are, in the well. */
    juce::Point<float> at (double hz, double db) { return { editor.well().xOf (hz), editor.well().yOf (db) }; }
};

int count (const ni::ui::test::FakeParameters& p, ni::ui::test::FakeParameters::Event::Kind kind, int index)
{
    int n = 0;
    for (const auto& e : p.events)
        n += e.kind == kind && e.index == index;
    return n;
}
} // namespace

TEST_CASE ("filter-designer: the window is its design's size, on its grid, and every line fits the bar")
{
    Rig rig;
    auto& e = rig.editor;
    CHECK (e.getWidth() == 888);
    CHECK (e.getHeight() == 708);
    CHECK (e.well().getBounds() == juce::Rectangle<int> (0, 0, 824, Editor::wellH));
    CHECK (e.row().getY() == Editor::rowY);
    CHECK (e.inspector().getY() == Editor::panelsY);
    CHECK (e.outputPanel().getX() == Editor::outputX);
    CHECK (e.outputPanel().getRight() == e.well().getRight());
    NI_CHECK_INFO_LIMIT (e);
}

TEST_CASE ("filter-designer: a double-click adds a filter where it is, of the type its place asks for")
{
    Rig rig;
    auto& well = rig.editor.well();
    Pointer pointer;
    pointer.doubleClick (well, rig.at (1000.0, 6.0));
    rig.frames();
    CHECK ((int) rig.plain (param::node (0, param::type)) == type::bell);
    CHECK (rig.plain (param::node (0, param::freq)) == doctest::Approx (1000.0).epsilon (0.03));
    CHECK (rig.plain (param::node (0, param::gain)) == doctest::Approx (6.0).epsilon (0.05));
    CHECK (rig.editor.selected() == 0);
    /* Below 30 Hz a High-Pass, in the next empty slot. */
    pointer.doubleClick (well, rig.at (15.0, 0.0));
    rig.frames();
    CHECK ((int) rig.plain (param::node (1, param::type)) == type::highPass);
    CHECK (rig.editor.selected() == 1);
    /* Each change one whole gesture. */
    CHECK (count (rig.model.params, ni::ui::test::FakeParameters::Event::Kind::begin, param::node (0, param::type)) == 1);
}

TEST_CASE ("filter-designer: dragging a handle moves Freq and Gain as one gesture each, Alt moves Q")
{
    Rig rig;
    rig.set (param::node (2, param::type), type::bell);
    rig.set (param::node (2, param::freq), 500.0);
    rig.frames();
    rig.model.params.clear();
    auto& well = rig.editor.well();
    const auto h = *well.handle (2);
    Pointer pointer;
    pointer.down (well, h);
    CHECK (rig.editor.selected() == 2);
    pointer.drag (h + juce::Point<float> (60.0f, -30.0f));
    pointer.drag (h + juce::Point<float> (80.0f, -40.0f));
    pointer.up();
    CHECK (rig.plain (param::node (2, param::freq)) > 600.0);
    CHECK (rig.plain (param::node (2, param::gain)) > 3.0);
    using K = ni::ui::test::FakeParameters::Event::Kind;
    CHECK (count (rig.model.params, K::begin, param::node (2, param::freq)) == 1);
    CHECK (count (rig.model.params, K::end, param::node (2, param::gain)) == 1);

    /* Alt: up is narrower. */
    rig.frames();
    const double q0 = rig.plain (param::node (2, param::q));
    const auto h2 = *well.handle (2);
    pointer.down (well, h2, juce::ModifierKeys::altModifier);
    pointer.drag (h2 + juce::Point<float> (0.0f, -48.0f));
    pointer.up();
    CHECK (rig.plain (param::node (2, param::q)) == doctest::Approx (2.0 * q0).epsilon (0.02));

    /* A press that does not move is no edit. */
    rig.model.params.clear();
    rig.frames();
    pointer.click (well, *well.handle (2));
    CHECK (rig.model.params.events.empty());
}

TEST_CASE ("filter-designer: an edge tick sets the width, the wheel the Q, a double-click resets, Delete empties")
{
    Rig rig;
    rig.set (param::node (0, param::type), type::notch);
    rig.set (param::node (0, param::freq), 2000.0);
    rig.frames();
    auto& well = rig.editor.well();
    const auto h = *well.handle (0);
    Pointer pointer;
    pointer.click (well, h);
    rig.frames();
    /* The upper edge, dragged out to an octave above: Q of a two-octave band. */
    const double upper = rig.model.picture (0).edgeHz[0][1];
    const juce::Point<float> tick (well.xOf (upper), h.y);
    pointer.down (well, tick);
    pointer.drag ({ well.xOf (4000.0), h.y });
    pointer.up();
    CHECK (rig.plain (param::node (0, param::q)) == doctest::Approx (rig.model.qForEdge (2000.0, 4000.0)).epsilon (0.03));

    const double q = rig.plain (param::node (0, param::q));
    Pointer::wheel (well, h, 0.25f);
    CHECK (rig.plain (param::node (0, param::q)) == doctest::Approx (q * std::pow (2.0, 0.5)).epsilon (0.02));

    rig.frames();
    pointer.doubleClick (well, *well.handle (0));
    CHECK (rig.plain (param::node (0, param::q)) == doctest::Approx (0.7071).epsilon (0.01));

    CHECK (ni::ui::gallery::key (well, juce::KeyPress::deleteKey));
    CHECK ((int) rig.plain (param::node (0, param::type)) == type::empty);
    CHECK (! ni::ui::gallery::key (well, juce::KeyPress::deleteKey));
}

TEST_CASE ("filter-designer: the caption's right end cycles the analyzer and the range")
{
    Rig rig;
    auto& well = rig.editor.well();
    Pointer pointer;
    const float y = 8.0f;
    CHECK (well.rangeDb() == 18);
    pointer.click (well, { (float) well.getWidth() - 12.0f, y });
    CHECK (well.rangeDb() == 30);
    /* Pre + post by default; a click goes to Post, then Pre, then off. */
    CHECK ((int) rig.plain (param::analyzer) == 3);
    pointer.click (well, { (float) well.getWidth() - 100.0f, y });
    CHECK ((int) rig.plain (param::analyzer) == 2);
    pointer.click (well, { (float) well.getWidth() - 100.0f, y });
    CHECK ((int) rig.plain (param::analyzer) == 1);
    well.cycleRange();
    CHECK (well.rangeDb() == 6);
}

TEST_CASE ("filter-designer: the row selects, and the inspector binds to the selection and lights what it uses")
{
    Rig rig;
    Pointer pointer;
    auto& cell = rig.editor.row().cellAt (4);
    pointer.click (cell, { 20.0f, 20.0f });
    CHECK (rig.editor.selected() == 4);
    CHECK (rig.editor.selectFor (param::node (4, param::type)) != nullptr);
    /* Empty: only the type is lit. */
    rig.frames();
    CHECK (! rig.editor.knobFor (param::node (4, param::freq))->isEnabled());
    CHECK (rig.editor.selectFor (param::node (4, param::type))->isEnabled());

    rig.set (param::node (4, param::type), type::lowPass);
    rig.set (param::node (4, param::family), 4.0);
    rig.frames();
    CHECK (rig.editor.knobFor (param::node (4, param::ripple))->isEnabled());
    CHECK (rig.editor.knobFor (param::node (4, param::stopband))->isEnabled());
    CHECK (! rig.editor.knobFor (param::node (4, param::gain))->isEnabled());
    CHECK (rig.editor.inspector().getTitle().isNotEmpty());

    rig.set (param::node (4, param::type), type::tilt);
    rig.frames();
    CHECK (rig.editor.knobFor (param::node (4, param::gain))->isEnabled());
    CHECK (! rig.editor.knobFor (param::node (4, param::q))->isEnabled());
    CHECK (! rig.editor.selectFor (param::node (4, param::family))->isEnabled());

    /* The isolator's cell. */
    pointer.click (rig.editor.row().cellAt (param::nodes), { 20.0f, 20.0f });
    CHECK (rig.editor.selected() == ResponseWell::isolator);
    CHECK (rig.editor.knobFor (param::isoLower) != nullptr);
    CHECK (rig.editor.toggleFor (param::isoOn) != nullptr);
    CHECK (rig.editor.knobFor (param::node (4, param::freq)) == nullptr);
}

TEST_CASE ("filter-designer: a choice of Order or Family lands on the order that plays")
{
    Rig rig;
    Pointer pointer;
    pointer.click (rig.editor.row().cellAt (2), { 20.0f, 20.0f });
    rig.set (param::node (2, param::type), type::lowPass);
    rig.set (param::node (2, param::order), 2.0);
    rig.frames();
    auto* family = rig.editor.selectFor (param::node (2, param::family));
    auto* order = rig.editor.selectFor (param::node (2, param::order));
    REQUIRE ((family != nullptr && order != nullptr));
    /* Linkwitz-Riley at 18 dB/oct plays LR4: the field says 24 dB/oct. */
    family->onChange (5);
    CHECK (rig.plain (param::node (2, param::family)) == 5.0);
    CHECK (rig.plain (param::node (2, param::order)) == 3.0);
    order->onChange (4);
    CHECK (rig.plain (param::node (2, param::order)) == 5.0);
    /* Back to Butterworth, every order is there. */
    family->onChange (0);
    order->onChange (4);
    CHECK (rig.plain (param::node (2, param::order)) == 4.0);
    /* A shelf stops at 24 dB/oct. */
    rig.set (param::node (2, param::type), type::lowShelf);
    rig.frames();
    rig.editor.selectFor (param::node (2, param::order))->onChange (7);
    CHECK (rig.plain (param::node (2, param::order)) == 3.0);
}

TEST_CASE ("filter-designer: the Bypass switch is the shell's bypass")
{
    Rig rig;
    auto* b = rig.editor.toggleFor (-1);
    REQUIRE (b != nullptr);
    CHECK (b->getParentComponent() == &rig.editor.outputPanel());
    Pointer pointer;
    pointer.click (*b, b->getLocalBounds().getCentre().toFloat());
    CHECK (rig.model.bypassParameter().getValue() == doctest::Approx (1.0f));
}

TEST_CASE ("filter-designer: the Matched switch sets Matched Cuts, under Adaptive")
{
    Rig rig;
    auto* m = rig.editor.toggleFor (param::matchedCuts);
    auto* a = rig.editor.toggleFor (param::adaptiveQ);
    REQUIRE ((m != nullptr && a != nullptr));
    CHECK (m->getParentComponent() == &rig.editor.outputPanel());
    CHECK ((m->getX() == a->getX() && m->getY() > a->getBottom()));
    CHECK (m->getBottom() <= rig.editor.outputPanel().getHeight());
    Pointer pointer;
    pointer.click (*m, m->getLocalBounds().getCentre().toFloat());
    CHECK (rig.plain (param::matchedCuts) == 1.0);
}

TEST_CASE ("filter-designer: with the isolator shown, its crossovers and its bands drag")
{
    Rig rig;
    rig.set (param::isoOn, 1.0);
    rig.editor.select (ResponseWell::isolator);
    rig.frames();
    auto& well = rig.editor.well();
    const auto area = well.plotArea();
    Pointer pointer;
    /* Lower, from 250 Hz to about 400. */
    pointer.down (well, { well.xOf (250.0), area.getCentreY() });
    pointer.drag ({ well.xOf (400.0), area.getCentreY() });
    pointer.up();
    CHECK (rig.plain (param::isoLower) == doctest::Approx (400.0).epsilon (0.03));
    /* Mid's level, down. */
    const juce::Point<float> mid (well.xOf (std::sqrt (400.0 * 2500.0)), well.yOf (0.0));
    pointer.down (well, mid);
    pointer.drag (mid + juce::Point<float> (0.0f, 60.0f));
    pointer.up();
    CHECK (rig.plain (param::isoMid) < -5.0);
    pointer.doubleClick (well, { mid.x, well.yOf (rig.plain (param::isoMid)) });
    CHECK (rig.plain (param::isoMid) == doctest::Approx (0.0).epsilon (0.01));
}

TEST_SUITE ("snapshot")
{
TEST_CASE ("filter-designer: the window, empty and with filters")
{
    Rig rig;
    rig.frames (2);
    NI_CHECK_SNAPSHOT (rig.editor, "filter-designer-default");
    rig.set (param::node (0, param::type), type::highPass);
    rig.set (param::node (0, param::freq), 30.0);
    rig.set (param::node (1, param::type), type::bell);
    rig.set (param::node (1, param::freq), 250.0);
    rig.set (param::node (1, param::gain), -3.0);
    rig.set (param::node (2, param::type), type::notch);
    rig.set (param::node (2, param::freq), 3200.0);
    rig.set (param::node (3, param::type), type::highShelf);
    rig.set (param::node (3, param::freq), 10000.0);
    rig.set (param::node (3, param::gain), 2.0);
    rig.set (param::node (4, param::type), type::lowPass);
    rig.set (param::node (4, param::family), 4.0);
    rig.set (param::node (4, param::freq), 18000.0);
    rig.editor.select (4);
    rig.frames (2);
    NI_CHECK_SNAPSHOT (rig.editor, "filter-designer-filters");
}
}

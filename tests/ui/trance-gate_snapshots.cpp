// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate's editor in its main states, as pictures:
 *
 *   trance-gate-rest       a fresh instance, the transport stopped
 *   trance-gate-playing    a drawn pattern with a tie and two amounts, the
 *                          release running past the step, playing
 *   trance-gate-order      Set order part way through, the fade half in, the
 *                          arrival numbers out, a number being typed
 *   trance-gate-fade-out   Fade Out with Soft: holes still sounding
 *   trance-gate-signal     the Signal tab, the sweep writing, an outcome in
 *                          the bar
 *   trance-gate-32         two rows of pads, the window grown by one
 *   trance-gate-settings   the settings row alone, slot 3 and Exponential
 *                          on their faces (UT3: the Slot select was empty)
 *
 * Each is the same engine stand-in (trance-gate_fakes.h renderCurves), so a
 * change in a picture is a change in the editor.
 */
#include "TranceGateEditor.h"

#include "Pointer.h"
#include "snapshot.h"
#include "trance-gate_fakes.h"

#include <doctest.h>

using namespace ni::tg;
using namespace ni::tg::test;
using ni::ui::gallery::Pointer;

namespace
{
struct Scene
{
    double ms = 0.0;
    FakeModel model;
    FakeClipboard clipboard;
    FakeFilePanels panels;
    TranceGateEditor editor { model, clipboard, panels, [this] { return ms; } };

    Scene()
    {
        /* The Ground at rest: its motion is the Ground's goldens' to hold. */
        editor.frame().ground().setReducedMotionQuery ([] { return true; });
        model.engineTransport.msPerStep = 125.0;
    }

    /* The engine's next block and renders, then one display frame. */
    void frame()
    {
        model.publish();
        renderCurves (model);
        ms += 1000.0 / 60.0;
        editor.tick (ms);
    }

    /* A drawn pattern: on, tie and two quieter steps. */
    void drawn()
    {
        auto& p = model.next;
        const char* cells = "x-x-hxxt-xx-x-qx";
        for (int i = 0; i < 16; ++i)
        {
            const char c = cells[i];
            p.steps[(std::size_t) i] = c == '-' ? StepMode::off : c == 't' ? StepMode::tie : StepMode::on;
            p.depths[(std::size_t) i] = c == 'h' ? 0.5f : c == 'q' ? 0.25f : 1.0f;
        }
        FakeModel::rankAll (p);
        p.cursor = 4;
    }
};
} // namespace

NI_SNAPSHOT_TEST ("trance-gate: at rest")
{
    Scene s;
    s.frame();
    NI_CHECK_SNAPSHOT (s.editor, "trance-gate-rest");
}

NI_SNAPSHOT_TEST ("trance-gate: a drawn pattern, playing, the release past the step")
{
    Scene s;
    s.drawn();
    s.model.set (param::amount, 80.0f);
    s.model.set (param::width, 70.0f);
    s.model.set (param::release, 60.0f);
    s.model.set (param::sustain, 60.0f);
    s.model.set (param::decay, 30.0f);
    s.model.engineTransport = { true, 5.4, 125.0 };
    s.frame();
    NI_CHECK_SNAPSHOT (s.editor, "trance-gate-playing");
}

NI_SNAPSHOT_TEST ("trance-gate: Set order part way through, the fade half in, a number being typed")
{
    Scene s;
    s.drawn();
    s.model.set (param::fade, 50.0f);
    s.frame();
    s.editor.orderButton().onClick();
    Pointer p;
    p.click (s.editor.pads().grid().step (6), { 20.0f, 20.0f });
    p.click (s.editor.pads().grid().step (0), { 20.0f, 20.0f });
    s.frame();
    REQUIRE (s.editor.pads().arrival (9) != nullptr);
    p.click (*s.editor.pads().arrival (9), { 3.0f, 3.0f });
    s.frame();
    NI_CHECK_SNAPSHOT (s.editor, "trance-gate-order");
}

NI_SNAPSHOT_TEST ("trance-gate: Fade Out, Soft, part way: holes still sounding")
{
    Scene s;
    s.drawn();
    s.model.set (param::fadeDir, 1.0f);
    s.model.set (param::fadeSoft, 1.0f);
    s.model.set (param::fade, 40.0f);
    s.frame();
    NI_CHECK_SNAPSHOT (s.editor, "trance-gate-fade-out");
}

NI_SNAPSHOT_TEST ("trance-gate: the Signal tab, the sweep writing, an outcome in the bar")
{
    Scene s;
    s.drawn();
    s.model.engineTransport = { true, 9.2, 125.0 };
    s.model.set (param::slot, 3.0f);
    s.frame();
    fillCapture (s.model, 256, 147, 2000.0);
    s.editor.band().tabs().onSelect (1);
    s.editor.verbs().copy();
    s.frame();
    NI_CHECK_SNAPSHOT (s.editor, "trance-gate-signal");
}

NI_SNAPSHOT_TEST ("trance-gate: 32 steps, two rows, the window grown")
{
    Scene s;
    s.drawn();
    auto& p = s.model.next;
    p.length = 32;
    for (int i = 16; i < 32; ++i)
    {
        p.steps[(std::size_t) i] = i % 3 == 0 ? StepMode::on : StepMode::off;
        p.depths[(std::size_t) i] = 1.0f;
    }
    FakeModel::rankAll (p);
    s.model.set (param::length, 32.0f);
    s.model.set (param::rate, 9.0f);
    s.model.engineTransport = { true, 20.6, 62.5 };
    s.frame();
    NI_CHECK_SNAPSHOT (s.editor, "trance-gate-32");
}

NI_SNAPSHOT_TEST ("trance-gate: the settings row, its selects showing the slot and the curve")
{
    Scene s;
    s.model.set (param::slot, 3.0f);
    s.model.set (param::curve, 1.0f);
    s.model.set (param::legato, 1.0f);
    s.frame();

    /* The row only, as the editor draws it: Slot to Time in %, with the
     * room a focus ring would take round it. */
    auto& e = s.editor;
    auto row = e.getLocalArea (&e.select (param::slot), e.select (param::slot).getLocalBounds());
    for (juce::Component* c : { (juce::Component*) &e.toggle (param::legato),
                                (juce::Component*) &e.select (param::curve),
                                (juce::Component*) &e.toggle (param::timeMode) })
        row = row.getUnion (e.getLocalArea (c, c->getLocalBounds()));
    const auto picture = ni::ui::test::render (e).getClippedImage (row.expanded (4));
    const auto result = ni::ui::test::compare (picture, "trance-gate-settings");
    CHECK_MESSAGE (result.ok, result.message.toStdString());
}

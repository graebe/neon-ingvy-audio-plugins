// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The parts of a note editor's well (docs/adr/0006): the time ruler, the
 * pitch rows, the notes as bars -- one plain, one selected and edited with
 * its recorded pitch ghosted under it, one with a gap where it has no pitch,
 * one muted -- the playhead, and the overview of the whole song with the view
 * on it.
 */
#include "Gallery.h"

#include "DisplayPage.h"
#include "NoteBars.h"
#include "Overview.h"
#include "PitchView.h"
#include "TimeView.h"

#include <cmath>

namespace
{
using namespace ni::ui;

constexpr double beat = 24000.0;

/* A note `beats` long from beat `at`, at `pitch`, recorded `cents` off and
 * with a vibrato or not, at `level`. */
NoteBars::Bar note (std::uint64_t id, double at, double beats, double pitch, double cents, float level, bool vibrato)
{
    NoteBars::Bar b;
    b.id = id;
    b.start = at * beat;
    b.end = (at + beats) * beat;
    b.centre = pitch + cents / 100.0;
    const int n = (int) (beats * 24.0);
    for (auto* c : { &b.level, &b.contour })
    {
        c->start = b.start;
        c->step = (b.end - b.start) / (double) n;
    }
    for (int i = 0; i <= n; ++i)
    {
        const double u = (double) i / (double) n;
        const double t = u * beats * 0.5;
        b.level.values.push_back (level * (float) std::min ({ 1.0, u * 12.0, (1.0 - u) * 8.0 }));
        const double v = vibrato && t > 0.2 ? 0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t) : 0.0;
        b.contour.values.push_back ((float) (b.centre + v - 0.6 * std::max (0.0, 0.12 - t) / 0.12));
    }
    return b;
}

struct NoteEditorPage final : public gallery::DisplayPage
{
    NoteEditorPage()
    {
        caption ("Ruler, rows, notes, playhead", { 0.0f, 0.0f, 600.0f, 14.0f });
        zoom::Axis time;
        time.lo = 0.0;
        time.hi = 64.0 * beat;
        time.show (0.0, 16.0 * beat);
        zoom::Axis pitch;
        pitch.lo = 40.0;
        pitch.hi = 90.0;
        pitch.show (58.0, 72.0);

        rows.setBounds (0, 24, 640, 280);
        rows.setAxis (pitch);
        rows.setScale (0b101010110101);   // C major
        addAndMakeVisible (rows);

        ruler.setBounds (PitchView::gutter, 24, 640 - PitchView::gutter, TimeView::height);
        ruler.setAxis (time);
        addAndMakeVisible (ruler);

        bars.setBounds (PitchView::gutter, 24 + TimeView::height, 640 - PitchView::gutter, 280 - TimeView::height);
        auto plotPitch = pitch;
        plotPitch.span = pitch.span * (double) bars.getHeight() / (double) rows.getHeight();
        plotPitch.start = pitch.end() - plotPitch.span;
        bars.setAxes (time, plotPitch);
        auto a = note (1, 0.0, 2.0, 64.0, -30.0, 0.8f, false);
        auto b = note (2, 2.5, 3.0, 67.0, 0.0, 1.0f, true);
        b.selected = true;
        b.ghost = b.contour;
        for (auto& v : b.ghost.values)
            v += 0.4f;
        auto c = note (3, 6.0, 2.5, 62.0, 20.0, 0.6f, true);
        for (size_t i = 20; i < 28; ++i)
            c.contour.values[i] = std::nanf ("");
        auto d = note (4, 9.5, 2.0, 65.0, 0.0, 0.9f, false);
        d.muted = true;
        bars.setBars ({ a, b, c, d });
        bars.setPlayhead (4.0 * beat);
        addAndMakeVisible (bars);

        caption ("Overview: two takes, the view on bars 1 to 4", { 0.0f, 320.0f, 600.0f, 14.0f });
        overview.setBounds (0, 340, 640, Overview::height);
        overview.setCaption ("Song   2 takes   64 bars");
        overview.setContent (0.0, 64.0 * 4.0 * beat);
        overview.setBlocks ({ { 0.0, 16.0 * 4.0 * beat }, { 32.0 * 4.0 * beat, 48.0 * 4.0 * beat } });
        std::vector<Overview::Tick> ticks;
        for (int i = 0; i < 40; ++i)
            ticks.push_back ({ i * 1.6 * beat, (i * 1.6 + 1.0) * beat, 0.5f + 0.4f * (float) std::sin (i * 1.7) });
        overview.setTicks (std::move (ticks));
        overview.setView (0.0, 16.0 * beat);
        addAndMakeVisible (overview);

        setSize (640, 400);
    }

    PitchView rows;
    TimeView ruler;
    NoteBars bars;
    Overview overview;
};
} // namespace

NI_GALLERY_PAGE ("Music", "Note Editor", [] { return std::make_unique<NoteEditorPage>(); });

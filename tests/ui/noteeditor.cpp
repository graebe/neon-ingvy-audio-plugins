// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A note editor's well, its parts one by one: the ruler's bar lines and its
 * pan, the pitch rows and their names, the bars' geometry and what a point
 * finds, and the overview moving the view. Then the page, as a picture.
 */
#include "NoteBars.h"
#include "Overview.h"
#include "PitchView.h"
#include "Pointer.h"
#include "TimeView.h"
#include "UvTokens.h"
#include "pages.h"

#include <doctest.h>

using namespace ni::ui;
using ni::ui::gallery::Pointer;

namespace
{
zoom::Axis axis (double lo, double hi, double start, double end)
{
    zoom::Axis a;
    a.lo = lo;
    a.hi = hi;
    a.show (start, end);
    return a;
}
} // namespace

TEST_CASE ("time view: bars and beats where the tempo puts them; a drag pans")
{
    TimeView::Tempo tempo { 24000.0, 12000.0, 4 };
    const auto a = axis (0.0, 1.0e7, 0.0, 8.0 * 24000.0);
    const auto lines = TimeView::lines (a, tempo, 800.0f);
    REQUIRE (lines.size() == 8);
    CHECK (lines[0].x == doctest::Approx (50.0f));
    CHECK ((lines[0].bar && lines[0].number == 1));
    CHECK_FALSE (lines[1].bar);
    CHECK ((lines[4].bar && lines[4].number == 2));
    /* Before the song: bar 0, -1, ... */
    const auto early = TimeView::lines (axis (-1.0e6, 1.0e7, -100000.0, 0.0), tempo, 800.0f);
    CHECK (early.front().number <= 0);

    TimeView v;
    v.setBounds (0, 0, 800, TimeView::height);
    v.setAxis (a);
    v.setTempo (tempo);
    CHECK (v.getTempo().beatsPerBar == 4);
    double asked = -1.0;
    v.onPan = [&] (double s) { asked = s; };
    Pointer p;
    p.down (v, { 400.0f, 10.0f });
    p.drag ({ 300.0f, 10.0f });
    p.up();
    CHECK (asked == doctest::Approx (100.0 * a.perPx (800.0)));
}

TEST_CASE ("pitch view: names in Live's numbering, rows up and down, a drag in the gutter pans, a double-click fits")
{
    CHECK (PitchView::nameOf (60) == "C3");
    CHECK (PitchView::nameOf (61) == "C#3");
    CHECK (PitchView::nameOf (-1) == "B-3");
    PitchView v;
    v.setBounds (0, 0, 400, 240);
    v.setAxis (axis (0.0, 127.0, 60.0, 72.0));
    v.setScale (0x0ab5);
    CHECK (v.getScale() == 0x0ab5);
    CHECK (v.yOf (72.0) == doctest::Approx (0.0f));
    CHECK (v.yOf (60.0) == doctest::Approx (240.0f));
    CHECK (v.pitchAt (120.0f) == doctest::Approx (66.0));
    double asked = -1.0;
    v.onPan = [&] (double s) { asked = s; };
    Pointer p;
    p.down (v, { 300.0f, 100.0f });
    p.drag ({ 300.0f, 120.0f });
    p.up();
    CHECK (asked < 0.0);
    p.down (v, { 10.0f, 100.0f });
    p.drag ({ 10.0f, 120.0f });
    p.up();
    CHECK (asked == doctest::Approx (61.0));

    /* A double-click in the gutter asks for a fit; past it, nothing. */
    int fits = 0;
    v.onFit = [&] { ++fits; };
    p.doubleClick (v, { 300.0f, 100.0f });
    CHECK (fits == 0);
    p.doubleClick (v, { 10.0f, 100.0f });
    CHECK (fits == 1);
}

TEST_CASE ("note bars: geometry, and what a point or a rectangle finds")
{
    NoteBars b;
    b.setBounds (0, 0, 1000, 240);
    b.setAxes (axis (0.0, 1.0e6, 0.0, 1000.0), axis (0.0, 127.0, 60.0, 72.0));
    NoteBars::Bar n;
    n.id = 7;
    n.start = 100.0;
    n.end = 300.0;
    n.centre = 66.0;
    n.level = { 100.0, 100.0, { 0.5f, 1.0f, 0.5f } };
    n.contour = { 100.0, 100.0, { 66.0f, std::nanf (""), 66.2f } };
    NoteBars::Bar m = n;
    m.id = 8;
    m.start = 500.0;
    m.end = 600.0;
    m.centre = 61.0;
    b.setBars ({ n, m });
    CHECK (b.getBars().size() == 2);
    CHECK (b.xOf (300.0) == doctest::Approx (300.0f));
    CHECK (b.yOf (66.0) == doctest::Approx (120.0f));
    CHECK (b.timeAt (500.0f) == doctest::Approx (500.0));
    CHECK (b.pitchAt (0.0f) == doctest::Approx (72.0));
    CHECK (b.halfThickness() == doctest::Approx (9.0f));
    CHECK (b.noteAt ({ 200.0f, 124.0f }) == std::optional<std::uint64_t> (7));
    CHECK_FALSE (b.noteAt ({ 200.0f, 160.0f }).has_value());
    CHECK_FALSE (b.noteAt ({ 400.0f, 120.0f }).has_value());
    CHECK (b.notesIn ({ 0.0f, 0.0f, 1000.0f, 240.0f }).size() == 2);
    CHECK (b.notesIn ({ 450.0f, 200.0f, 200.0f, 30.0f }) == std::vector<std::uint64_t> { 8 });
    b.setPlayhead (400.0);
    b.setPlayhead (std::nullopt);

    /* Handles only where a bar has them: its two ends, on its row. */
    CHECK_FALSE (b.edgeAt ({ 300.0f, 120.0f }).has_value());
    auto withHandles = b.getBars();
    withHandles[0].handles = true;
    b.setBars (withHandles);
    CHECK (b.edgeAt ({ 302.0f, 121.0f }) == std::optional<NoteBars::Edge> ({ 7, true }));
    CHECK (b.edgeAt ({ 99.0f, 120.0f }) == std::optional<NoteBars::Edge> ({ 7, false }));
    CHECK_FALSE (b.edgeAt ({ 200.0f, 120.0f }).has_value());
    CHECK_FALSE (b.edgeAt ({ 300.0f, 150.0f }).has_value());
    CHECK_FALSE (b.edgeAt ({ 600.0f, 220.0f }).has_value());
    CHECK (b.boundsOf (7) == juce::Rectangle<float> (100.0f, 111.0f, 200.0f, 18.0f));
    CHECK (b.boundsOf (9).isEmpty());
    b.setMarker (NoteBars::Marker { 7, 200.0 });
    b.setMarker (std::nullopt);
}

TEST_CASE ("note bars: a bar's handles and a marker are drawn in uv, where they are")
{
    NoteBars b;
    b.setBounds (0, 0, 400, 120);
    b.setAxes (axis (0.0, 1.0e6, 0.0, 400.0), axis (0.0, 127.0, 60.0, 66.0));
    NoteBars::Bar n;
    n.id = 1;
    n.start = 100.0;
    n.end = 300.0;
    n.centre = 63.0;
    n.handles = true;
    b.setBars ({ n });
    b.setMarker (NoteBars::Marker { 1, 200.0 });
    juce::Image img (juce::Image::ARGB, 400, 120, true, juce::SoftwareImageType());
    {
        juce::Graphics g (img);
        b.paintEntireComponent (g, false);
    }
    const auto lit = uv::tok::colour::uv;
    const auto isUv = [&] (juce::Colour p)
    { return p.getAlpha() == 255 && std::abs ((int) p.getRed() - (int) lit.getRed()) <= 2 && std::abs ((int) p.getBlue() - (int) lit.getBlue()) <= 2; };
    CHECK (isUv (img.getPixelAt (100, 60)));   // the start handle
    CHECK (isUv (img.getPixelAt (300, 60)));   // the end handle
    CHECK (isUv (img.getPixelAt (200, 52)));   // the marker, across the bar
    CHECK_FALSE (isUv (img.getPixelAt (150, 52)));

    /* Two bars marked as one run: linked, end to start, centre to centre. */
    NoteBars::Bar m = n;
    m.id = 2;
    m.start = 340.0;
    m.end = 390.0;
    m.handles = false;
    n.handles = false;
    n.marked = m.marked = true;
    b.setBars ({ n, m });
    b.setMarker (std::nullopt);
    juce::Image run (juce::Image::ARGB, 400, 120, true, juce::SoftwareImageType());
    {
        juce::Graphics g (run);
        b.paintEntireComponent (g, false);
    }
    CHECK (run.getPixelAt (320, 60).getAlpha() > 0);   // the link between them
    n.marked = m.marked = false;
    b.setBars ({ n, m });
    juce::Image plain (juce::Image::ARGB, 400, 120, true, juce::SoftwareImageType());
    {
        juce::Graphics g (plain);
        b.paintEntireComponent (g, false);
    }
    CHECK (plain.getPixelAt (320, 60).getAlpha() == 0);
    CHECK (run.getPixelAt (200, 51) != plain.getPixelAt (200, 51));   // a marked bar's hairline is uv
}

TEST_CASE ("note bars: a muted note is its outline alone -- no fill, no pitch, no ghost -- and still found")
{
    NoteBars b;
    b.setBounds (0, 0, 400, 120);
    b.setAxes (axis (0.0, 1.0e6, 0.0, 400.0), axis (0.0, 127.0, 60.0, 66.0));
    NoteBars::Bar n;
    n.id = 1;
    n.start = 100.0;
    n.end = 300.0;
    n.centre = 63.0;
    n.level = { 100.0, 100.0, { 1.0f, 1.0f, 1.0f } };
    n.contour = { 100.0, 10.0, std::vector<float> (21, 63.0f) };
    n.ghost = { 100.0, 10.0, std::vector<float> (21, 62.5f) };
    const auto paint = [&b] (const NoteBars::Bar& bar)
    {
        b.setBars ({ bar });
        juce::Image img (juce::Image::ARGB, 400, 120, true, juce::SoftwareImageType());
        juce::Graphics g (img);
        b.paintEntireComponent (g, false);
        return img;
    };
    const auto shown = paint (n);
    n.muted = true;
    const auto muted = paint (n);

    const float y = b.yOf (63.0), ghostY = b.yOf (62.5);
    const int top = (int) b.boundsOf (1).getY();
    CHECK (shown.getPixelAt (150, (int) y).getAlpha() > 0);         // the pitch
    CHECK (shown.getPixelAt (150, top + 3).getAlpha() > 0);         // the fill
    CHECK (shown.getPixelAt (150, (int) ghostY).getAlpha() > 0);    // the ghost
    CHECK (muted.getPixelAt (150, (int) y).getAlpha() == 0);
    CHECK (muted.getPixelAt (150, top + 3).getAlpha() == 0);
    CHECK (muted.getPixelAt (150, (int) ghostY).getAlpha() == 0);
    CHECK (muted.getPixelAt (150, top).getAlpha() > 0);             // the outline
    CHECK (b.noteAt ({ 200.0f, y }) == std::optional<std::uint64_t> (1));
}

TEST_CASE ("overview: content to x and back; a press centres the view there")
{
    Overview o;
    o.setBounds (0, 0, 812, Overview::height);
    o.setContent (0.0, 800.0);
    o.setView (0.0, 100.0);
    CHECK (o.plot().getWidth() == doctest::Approx (800.0f));
    CHECK (o.xOf (400.0) == doctest::Approx (406.0f));
    CHECK (o.timeAt (406.0f) == doctest::Approx (400.0));
    double asked = -1.0;
    o.onView = [&] (double s) { asked = s; };
    Pointer p;
    p.down (o, { 406.0f, 30.0f });
    CHECK (asked == doctest::Approx (350.0));
    p.drag ({ 506.0f, 30.0f });
    CHECK (asked == doctest::Approx (450.0));
    p.up();
}

NI_SNAPSHOT_TEST ("note editor: ruler, rows, note bars and the overview")
{
    NI_CHECK_PAGE ("Note Editor", "music-note-editor");
}

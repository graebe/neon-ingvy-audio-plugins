// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The panning well's maths (ZoomMath.h, docs/adr/0006), with no window:
 * a zoom keeps what is under the pointer under it, the view never leaves the
 * content by more than its margin, the span stays in its limits, and the
 * grid thins out by whole bars when beats would crowd.
 */
#include "ZoomMath.h"

#include <doctest.h>

using ni::ui::zoom::Axis;

namespace
{
Axis song()
{
    Axis a;
    a.lo = 0.0;
    a.hi = 1000.0;
    a.minSpan = 10.0;
    a.margin = 0.05;
    a.show (100.0, 300.0);
    return a;
}
} // namespace

TEST_CASE ("zoom math: pixels and back")
{
    const auto a = song();
    CHECK (a.toPx (200.0, 400.0) == doctest::Approx (200.0));
    CHECK (a.fromPx (100.0, 400.0) == doctest::Approx (150.0));
    CHECK (a.perPx (400.0) == doctest::Approx (0.5));
    CHECK (a.end() == doctest::Approx (300.0));
}

TEST_CASE ("zoom math: a zoom keeps its anchor where it was")
{
    auto a = song();
    const double anchor = 250.0;
    const double px = a.toPx (anchor, 400.0);
    a.zoom (2.0, anchor);
    CHECK (a.span == doctest::Approx (100.0));
    CHECK (a.toPx (anchor, 400.0) == doctest::Approx (px));
    a.zoom (0.5, anchor);
    CHECK (a.span == doctest::Approx (200.0));
    a.zoom (0.0, anchor);
    CHECK (a.span == doctest::Approx (200.0));
}

TEST_CASE ("zoom math: the limits -- span and margins")
{
    auto a = song();
    a.zoom (1000.0, 200.0);
    CHECK (a.span == doctest::Approx (10.0));
    a.zoom (1.0e-6, 200.0);
    CHECK (a.span == doctest::Approx (a.widest()));
    CHECK (a.widest() == doctest::Approx (1000.0 / 0.9));
    a.show (100.0, 300.0);
    a.pan (-1.0e6);
    CHECK (a.start == doctest::Approx (-0.05 * 200.0));
    a.pan (1.0e6);
    CHECK (a.end() == doctest::Approx (1000.0 + 0.05 * 200.0));
    /* Content narrower than the view sits at its start. */
    Axis small;
    small.lo = 0.0;
    small.hi = 10.0;
    small.maxSpan = 1.0e9;
    small.minSpan = 100.0;
    small.show (50.0, 150.0);
    CHECK (small.span == doctest::Approx (100.0));
    CHECK (small.start == doctest::Approx (-5.0));
    small.setContent (0.0, 5000.0);
    CHECK (small.hi == 5000.0);
}

TEST_CASE ("zoom math: a fit centres the range with its room, within the limits")
{
    Axis pitch;
    pitch.lo = 24.0;
    pitch.hi = 108.0;
    pitch.margin = 0.0;
    pitch.minSpan = 6.0;
    pitch.maxSpan = 48.0;
    pitch.show (54.0, 78.0);
    pitch.fit (60.0, 67.0, 2.0);
    CHECK (pitch.start == doctest::Approx (58.0));
    CHECK (pitch.span == doctest::Approx (11.0));
    /* One pitch: the narrowest view, about it. */
    pitch.fit (64.0, 64.0, 1.0);
    CHECK (pitch.span == doctest::Approx (6.0));
    CHECK (pitch.start == doctest::Approx (61.0));
    /* Wider than the widest: the widest, about the middle. */
    pitch.fit (30.0, 100.0, 2.0);
    CHECK (pitch.span == doctest::Approx (48.0));
    CHECK (pitch.start + 0.5 * pitch.span == doctest::Approx (65.0));
    /* Past the content's end: held inside it. */
    pitch.fit (104.0, 107.0, 2.0);
    CHECK (pitch.end() == doctest::Approx (108.0));
}

TEST_CASE ("zoom math: the grid keeps lines apart, by whole bars")
{
    Axis a;
    a.lo = 0.0;
    a.hi = 1.0e7;
    a.show (0.0, 16.0 * 24000.0);   // sixteen beats
    auto g = ni::ui::zoom::grid (a, 800.0, 0.0, 24000.0, 4, 12.0);
    CHECK (g.every == 1);
    CHECK (g.first == 0);
    CHECK (g.count == 17);
    a.show (0.0, 400.0 * 24000.0);  // 400 beats on 800 px: 2 px a beat
    g = ni::ui::zoom::grid (a, 800.0, 0.0, 24000.0, 4, 12.0);
    CHECK (g.every % 4 == 0);
    CHECK ((double) g.every * 2.0 >= 12.0);
    /* A view starting between lines starts at the next one. */
    a.show (30000.0, 30000.0 + 8.0 * 24000.0);
    g = ni::ui::zoom::grid (a, 800.0, 0.0, 24000.0, 4, 12.0);
    CHECK (g.first == 2);
    CHECK (ni::ui::zoom::grid (a, 0.0, 0.0, 24000.0, 4, 12.0).count == 0);
    CHECK (ni::ui::zoom::wheelFactor (0.0) == doctest::Approx (1.0));
    CHECK (ni::ui::zoom::wheelFactor (0.125) > 1.2);
}

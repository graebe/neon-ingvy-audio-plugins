// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The maths of a well that pans and zooms its content (docs/adr/0006): one
 * axis of a view onto content, as numbers, with no window in it -- so it is
 * tested on its own and every panning well shares it (TimeView, PitchView,
 * NoteBars, Overview).
 *
 * AN AXIS is the stretch [start, start + span) of the content's units -- host
 * samples, MIDI pitch -- that the well shows across `size` pixels. The content
 * runs from `lo` to `hi`. The rules ADR 0006 sets are kept by clamp(), which
 * every move ends with:
 *
 *   - the span stays between `minSpan` and `maxSpan`, and never wider than
 *     the content with its margins;
 *   - the view never shows more than `margin` of its span past either end of
 *     the content;
 *   - content narrower than the view sits at the view's start.
 *
 * A ZOOM keeps the content under the pointer under the pointer: its `anchor`
 * is in content units, and stays at the same pixel. A FIT shows a range of
 * the content, centred, with room either side.
 *
 * Plain C++ and header-only: the editors and the tests include it without
 * JUCE.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace ni::ui::zoom
{

struct Axis
{
    double start = 0.0;
    double span = 1.0;
    /* The content's ends. */
    double lo = 0.0;
    double hi = 1.0;
    double minSpan = 1.0e-9;
    double maxSpan = 1.0e300;
    /* How much of the span may show past an end. */
    double margin = 0.05;

    double end() const noexcept { return start + span; }

    /* Content to pixels across `size`, and back. */
    double toPx (double v, double size) const noexcept { return (v - start) / span * size; }
    double fromPx (double x, double size) const noexcept { return start + x / size * span; }
    /* Content units per pixel. */
    double perPx (double size) const noexcept { return span / std::max (1.0, size); }

    /* The widest span the content allows, margins included. */
    double widest() const noexcept { return std::max (minSpan, (hi - lo) / std::max (1.0e-12, 1.0 - 2.0 * margin)); }

    void clamp() noexcept
    {
        span = std::clamp (span, minSpan, std::max (minSpan, std::min (maxSpan, widest())));
        const double first = lo - margin * span;
        const double last = std::max (first, hi + margin * span - span);
        start = std::clamp (start, first, last);
    }

    /* Moves the view by `by` content units. */
    void pan (double by) noexcept
    {
        start += by;
        clamp();
    }

    /* Narrows the view by `factor` (above 1 zooms in) about `anchor`. */
    void zoom (double factor, double anchor) noexcept
    {
        if (! (factor > 0.0))
            return;
        const double was = span;
        span = std::clamp (span / factor, minSpan, std::max (minSpan, std::min (maxSpan, widest())));
        start = anchor - (anchor - start) * span / was;
        clamp();
    }

    /* Shows content `a` to `b`, as far as the limits allow. */
    void show (double a, double b) noexcept
    {
        start = std::min (a, b);
        span = std::abs (b - a);
        clamp();
    }

    /* Fits content `a` to `b` with `pad` either side, centred, as far as
     * the limits allow: too narrow a range widens about its middle. */
    void fit (double a, double b, double pad) noexcept
    {
        const double mid = 0.5 * (a + b);
        span = std::clamp (std::abs (b - a) + 2.0 * pad, minSpan, std::max (minSpan, std::min (maxSpan, widest())));
        start = mid - 0.5 * span;
        clamp();
    }

    /* Where the content is now: a new `lo`, `hi`, kept in view. */
    void setContent (double newLo, double newHi) noexcept
    {
        lo = std::min (newLo, newHi);
        hi = std::max (newLo, newHi);
        clamp();
    }
};

/* The zoom factor of a wheel's or a pinch's movement: a notch of the wheel
 * (deltaY 1/8 on macOS's reading) is 1.25 times, a pinch its own scale. */
inline double wheelFactor (double deltaY) noexcept
{
    return std::exp2 (deltaY * 2.6);
}

/*
 * Grid lines for an axis whose units come in steps of `unit` (a beat: so
 * many samples), grouped in `group` (beats to a bar): the first unit index
 * at or after the view's start and every `every`-th one after it, as few as
 * keep at least `minPx` between two lines across `size` pixels. `every` is
 * 1, or a multiple of `group` when single units would crowd -- a bar line
 * stays a bar line.
 */
struct Grid
{
    long long first = 0;
    long long every = 1;
    long long count = 0;
};

inline Grid grid (const Axis& a, double size, double origin, double unit, int group, double minPx) noexcept
{
    Grid g;
    if (! (unit > 0.0) || size <= 0.0)
        return g;
    const double px = unit / a.perPx (size);
    long long every = 1;
    if (px < minPx)
    {
        const long long bar = std::max (1, group);
        every = bar;
        while ((double) every * px < minPx)
            every *= 2;
    }
    const double firstUnit = std::ceil ((a.start - origin) / unit / (double) every) * (double) every;
    const double lastUnit = std::floor ((a.end() - origin) / unit / (double) every) * (double) every;
    g.first = (long long) firstUnit;
    g.every = every;
    g.count = lastUnit >= firstUnit ? (long long) ((lastUnit - firstUnit) / (double) every) + 1 : 0;
    return g;
}

} // namespace ni::ui::zoom

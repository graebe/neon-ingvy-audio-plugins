// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The plot primitives. Plot.h has the card and the rules.
 */
#include "Plot.h"

#include "UvLight.h"
#include "UvType.h"
#include "WaveSource.h"

#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace ni::ui
{

namespace c = uv::tok::colour;

namespace plot
{

/* --------------------------------------------------------------- marks -- */

void curve (juce::Graphics& g, const juce::Path& line)
{
    /* .curve { stroke-width: 2; stroke-linejoin: round } over the arc glow. */
    juce::Path stroke;
    juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt)
        .createStrokedPath (stroke, line);
    uv::light::glowArc (g, stroke);
    g.setColour (c::uv);
    g.fillPath (stroke);
}

void under (juce::Graphics& g, const juce::Path& area)
{
    g.setColour (c::plotFill);
    g.fillPath (area);
}

void ghost (juce::Graphics& g, const juce::Path& line)
{
    g.setColour (c::plotGhost);
    g.strokePath (line, juce::PathStrokeType (uv::tok::stroke::strokeHair));
}

void dry (juce::Graphics& g, const juce::Path& area)
{
    /* .dry { fill: plot-dry; opacity: .5 } */
    g.setColour (c::plotDry.withMultipliedAlpha (0.5f));
    g.fillPath (area);
}

void wet (juce::Graphics& g, const juce::Path& area)
{
    uv::light::glowArc (g, area);
    g.setColour (c::uv);
    g.fillPath (area);
}

void rule (juce::Graphics& g, float x, float top, float bottom, bool warn)
{
    /* .rule, and .rule.warn: centred on x, as an SVG line is. */
    g.setColour (warn ? c::amber : c::line200);
    g.drawLine (x, top, x, bottom, warn ? 2.0f : uv::tok::stroke::strokeHair);
}

/* ---------------------------------------------------------------- band -- */

namespace
{
/* The extremes of the capture columns behind screen pixel `px`, or false
 * when none of them holds data. */
bool extremes (const Capture& cap, int loIndex, int hiIndex, int px, int width,
               const std::function<bool (int)>& seen, float& lo, float& hi)
{
    const int total = cap.count;
    const int a = (int) (((long long) px * total) / width);
    const int b = juce::jmax (a + 1, (int) (((long long) (px + 1) * total) / width));
    lo = std::numeric_limits<float>::infinity();
    hi = -std::numeric_limits<float>::infinity();
    bool any = false;
    for (int i = a; i < b && i < total; ++i)
    {
        if (seen && ! seen (i))
            continue;
        const float* column = cap.data + (std::ptrdiff_t) i * cap.stride;
        lo = juce::jmin (lo, column[loIndex]);
        hi = juce::jmax (hi, column[hiIndex]);
        any = true;
    }
    return any;
}
} // namespace

void band (juce::Path& out, const Capture& cap, int loIndex, int hiIndex, const BandGeometry& geom,
           const std::function<bool (int)>& seen)
{
    out.clear();
    if (cap.data == nullptr || cap.count < 2 || geom.width <= 0
        || loIndex < 0 || hiIndex < 0 || loIndex >= cap.stride || hiIndex >= cap.stride)
        return;

    const float mid = (geom.top + geom.bottom) * 0.5f;
    const float half = (geom.bottom - geom.top) * 0.5f;
    const float scale = geom.fullScale > 0.0f ? 1.0f / geom.fullScale : 1.0f;
    const auto y = [mid, half, scale] (float v) { return mid - half * juce::jlimit (-1.0f, 1.0f, v * scale); };

    /*
     * RUNS OF DRAWN PIXELS, each its own closed sub-path, so a gap is a gap
     * and not a line across one; a run of one pixel is a point, not a
     * region, and is left out. A run is found first, then its top edge goes
     * forward and its bottom edge comes back over the same pixels -- the
     * extremes worked out again each time rather than kept: a cheap loop run
     * three times, and no storage to allocate per frame.
     */
    float lo = 0.0f, hi = 0.0f;
    const auto drawn = [&] (int px) { return extremes (cap, loIndex, hiIndex, px, geom.width, seen, lo, hi); };

    for (int px = 0; px < geom.width;)
    {
        if (! drawn (px))
        {
            ++px;
            continue;
        }
        const int start = px;
        while (px + 1 < geom.width && drawn (px + 1))
            ++px;
        const int end = px++;
        if (end == start)
            continue;

        for (int x = start; x <= end; ++x)
        {
            drawn (x);
            if (x == start)
                out.startNewSubPath (geom.x0 + (float) x, y (hi));
            else
                out.lineTo (geom.x0 + (float) x, y (hi));
        }
        for (int x = end; x >= start; --x)
        {
            drawn (x);
            out.lineTo (geom.x0 + (float) x, y (lo));
        }
        out.closeSubPath();
    }
}

/* --------------------------------------------------------------- level -- */

float peak (const Capture& cap, std::initializer_list<int> fields, const std::function<bool (int)>& seen)
{
    float out = 0.0f;
    if (cap.data == nullptr)
        return out;
    for (int i = 0; i < cap.count; ++i)
    {
        if (seen && ! seen (i))
            continue;
        const float* column = cap.data + (std::ptrdiff_t) i * cap.stride;
        for (const int f : fields)
            if (f >= 0 && f < cap.stride)
                out = juce::jmax (out, std::abs (column[f]));
    }
    return out;
}

namespace
{
/* No level at all is lower than any rung. */
constexpr float silenceDb = -1000.0f;

float toDb (float level)
{
    return level > 0.0f && std::isfinite (level) ? 20.0f * std::log10 (level) : silenceDb;
}
} // namespace

float LevelRange::fit (float peakLevel)
{
    /* A peak within a hundredth of a decibel of a rung is on it: a level
     * written as -24 dBFS comes back from the logarithm a hair either side,
     * and must not land a whole rung up for the hair. */
    constexpr float onRung = 0.01f;
    const float peakDb = toDb (peakLevel);
    if (peakDb >= -onRung)
        return 0.0f;
    return juce::jmax (floorDb, std::ceil ((peakDb - onRung) / stepDb) * stepDb);
}

/*
 * THE WINDOW'S PEAK IN BUCKETS: a quarter second each, the loudest peak in
 * it, keyed by which quarter second of the clock it is. The window is the
 * buckets of the last two seconds, so its peak is found without keeping
 * every frame's -- and a bucket from a lap ago is recognised by its key and
 * started again rather than read.
 */
void LevelRange::record (float peakDb, double nowMs)
{
    latest = (long long) std::floor (nowMs / bucketMs);
    const auto slot = (std::size_t) (((latest % buckets) + buckets) % buckets);
    if (heldIn[slot] != latest)
    {
        heldIn[slot] = latest;
        held[slot] = silenceDb;
    }
    held[slot] = juce::jmax (held[slot], peakDb);
}

float LevelRange::windowPeakDb() const
{
    float out = silenceDb;
    for (std::size_t i = 0; i < held.size(); ++i)
        if (heldIn[i] > latest - buckets && heldIn[i] <= latest)
            out = juce::jmax (out, held[i]);
    return out;
}

bool LevelRange::follow (float peakLevel, double nowMs)
{
    const float before = db();
    const float now = fit (peakLevel);

    if (! started)
    {
        /* The first frame lands on its rung: a well opened on quiet material
         * is zoomed already, not zooming. */
        held.fill (silenceDb);
        heldIn.fill (std::numeric_limits<long long>::min());
        record (toDb (peakLevel), nowMs);
        started = true;
        current = now;
        lastMs = nowMs;
        return ! juce::exactlyEqual (before, db());
    }

    record (toDb (peakLevel), nowMs);
    const double dt = juce::jlimit (0.0, 1000.0, nowMs - lastMs);
    lastMs = nowMs;

    if (now > current)
    {
        /* Attack: louder than the range, so wider at once. */
        current = now;
    }
    else
    {
        /* Release: towards the rung the window's peak fits under with the
         * margin to spare, and only ever narrower -- a rung above the range
         * is the margin's doing, no reason to widen, and that is the
         * hysteresis. */
        const float target = fit (std::pow (10.0f, (windowPeakDb() + marginDb) / 20.0f));
        if (target < current)
        {
            current = target + (current - target) * (float) std::exp (-dt / releaseMs);
            if (current - target < 0.1f)
                current = target;
        }
    }
    return ! juce::exactlyEqual (before, db());
}

void LevelRange::setFixed (bool on)
{
    fixed = on;
}

float LevelRange::fullScale() const
{
    return std::pow (10.0f, db() / 20.0f);
}

juce::String LevelRange::label() const
{
    const int whole = juce::roundToInt (db());
    /* U+2212, the minus sign, as the type has it; never a hyphen. */
    auto text = whole < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + juce::String (-whole)
                          : juce::String ("0");
    text << " dB";
    if (fixed)
        text << " fixed";
    return text;
}

/* --------------------------------------------------------------- ruler -- */

namespace
{
constexpr std::array<double, 14> ladder { 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000 };
/* A tick this close to the landmark gives way to it rather than overprint it. */
constexpr float giveWay = 34.0f;
/* Half a hint glyph's advance, for keeping a centred label inside the well. */
constexpr float halfGlyph = 3.0f;
} // namespace

std::vector<Tick> axisTicks (float widthPx, double spanMs, double markMs)
{
    std::vector<Tick> out;
    if (! (spanMs > 0.0) || ! std::isfinite (spanMs) || widthPx < 8.0f)
        return out;

    double step = ladder.back();
    for (const double c : ladder)
        if ((double) widthPx * (c / spanMs) >= (double) tickSpacing)
        {
            step = c;
            break;
        }

    std::optional<double> taken;
    if (markMs > 0.0 && markMs <= spanMs)
    {
        const bool whole = std::abs (markMs - std::round (markMs)) < 0.05;
        /* juce::String (x, 0) is not "no decimals" but "as many as it takes". */
        const auto text = whole ? juce::String (juce::roundToInt (markMs)) : juce::String (markMs, 1);
        out.push_back ({ markMs, text + " ms" });
        taken = markMs / spanMs;
    }

    /* Counted, not accumulated: a sum of steps drifts off the ladder. */
    for (int k = 0;; ++k)
    {
        const double ms = step * k;
        if (ms > spanMs + 1.0e-6)
            break;
        const double f = ms / spanMs;
        if (taken && std::abs (f - *taken) * widthPx < giveWay)
            continue;
        out.push_back ({ ms, juce::String (juce::roundToInt (ms)) });
    }
    return out;
}

void Axis::set (float wellWidth, double spanMs, double markMs)
{
    if (juce::exactlyEqual (wellWidth, width) && juce::exactlyEqual (spanMs, span)
        && juce::exactlyEqual (markMs, mark))
        return;
    width = wellWidth;
    span = spanMs;
    mark = markMs;
    list = axisTicks (width - 2.0f * inset, span, mark);
}

float Axis::xOf (double ms) const
{
    if (! (span > 0.0))
        return inset;
    return inset + (width - 2.0f * inset) * (float) (ms / span);
}

void Axis::paint (juce::Graphics& g, float y) const
{
    g.setColour (c::line200);
    g.drawLine (inset, y, width - inset, y, uv::tok::stroke::strokeHair);

    const auto font = uv::type::hint();
    g.setFont (font);
    for (const auto& t : list)
    {
        const float x = xOf (t.ms);
        g.setColour (c::line200);
        g.drawLine (x, y, x, y + tickLength, uv::tok::stroke::strokeHair);

        /* Nudged in at the ends so a label never hangs outside the well. */
        const float half = (float) t.text.length() * halfGlyph;
        const float at = juce::jmin (width - inset - half, juce::jmax (inset + half, x));
        g.setColour (c::inkMuted);
        g.drawSingleLineText (t.text, juce::roundToInt (at), juce::roundToInt (y + labelDrop),
                              juce::Justification::horizontallyCentred);
    }
}

} // namespace plot

/* ---------------------------------------------------------------- well -- */

namespace
{
/* The caption's baseline: in the 14px band, as the web well sets it. */
constexpr float captionBaseline = 11.0f;
} // namespace

PlotWell::PlotWell()
{
    setWaveSource (*this);
    setWantsKeyboardFocus (false);
}

PlotWell::~PlotWell() = default;

void PlotWell::setCaption (const juce::String& text)
{
    const auto upper = text.toUpperCase();
    if (upper == caption)
        return;
    caption = upper;
    setTitle (caption);
    repaint();
}

juce::Rectangle<float> PlotWell::contentBounds() const
{
    const auto b = getLocalBounds().toFloat();
    return { plot::inset, plot::captionH, juce::jmax (0.0f, b.getWidth() - 2.0f * plot::inset),
             juce::jmax (0.0f, b.getHeight() - plot::captionH - plot::inset) };
}

float PlotWell::xAt (float fraction) const
{
    const auto content = contentBounds();
    return content.getX() + content.getWidth() * juce::jlimit (0.0f, 1.0f, fraction);
}

void PlotWell::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (c::bg000);
    g.fillRect (b);
    g.setColour (c::line100);
    g.drawRect (b, uv::tok::stroke::strokeHair);

    {
        juce::Graphics::ScopedSaveState state (g);
        paintPlot (g);
    }

    g.setFont (uv::type::hint());
    g.setColour (c::inkMuted);
    if (caption.isNotEmpty())
        g.drawSingleLineText (caption, (int) plot::inset, (int) captionBaseline);

    /* The range, on the caption's baseline at the far end: a label of the
     * trace's top edge, which is just under it. */
    if (levelShown)
        g.drawSingleLineText (level.label(), juce::roundToInt ((float) getWidth() - plot::inset - levelInset),
                              (int) captionBaseline, juce::Justification::right);
}

void PlotWell::enableLevelRange (float labelInset)
{
    levelShown = true;
    levelInset = juce::jmax (0.0f, labelInset);
    invalidateAccessibilityHandler();
    repaint();
}

bool PlotWell::followLevel (float peakLevel, double nowMs)
{
    /* The label is whole decibels: compared as those, so a frame builds no
     * text it would only throw away. */
    const int shown = juce::roundToInt (level.db());
    const bool moved = level.follow (peakLevel, nowMs);
    if (levelShown && juce::roundToInt (level.db()) != shown)
        repaint (0, 0, getWidth(), (int) plot::captionH);
    return moved;
}

void PlotWell::setLevelFixed (bool on)
{
    if (on == level.isFixed())
        return;
    level.setFixed (on);
    levelChanged();
    repaint();
}

void PlotWell::mouseDoubleClick (const juce::MouseEvent&)
{
    if (levelShown)
        setLevelFixed (! level.isFixed());
}

std::unique_ptr<juce::AccessibilityHandler> PlotWell::createAccessibilityHandler()
{
    /* A picture: the caption names it, the info line (setInfo) describes it.
     * One with a level range takes a press, which is the double-click. */
    juce::AccessibilityActions actions;
    if (levelShown)
        actions.addAction (juce::AccessibilityActionType::press, [this] { setLevelFixed (! level.isFixed()); });
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::image, std::move (actions));
}

} // namespace ni::ui

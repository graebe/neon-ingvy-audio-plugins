// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Filter Designer's response well. ResponseWell.h says what it shows and
 * what the pointer does.
 */
#include "ResponseWell.h"

#include "InfoLines.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

#include <cmath>

namespace ni::fd
{

namespace
{
namespace c = uv::tok::colour;

/* The analyzer's own scale, in dBFS: its top and its depth. */
constexpr double analyzerTop = -6.0, analyzerSpan = 78.0;
/* How far a press may move before it is a drag. */
constexpr float deadZone = 3.0f;
/* How near the pointer must be to take a handle. */
constexpr float reach = 9.0f;
/* Shift at the press: five times finer. */
constexpr double fineRatio = 5.0;
constexpr double defaultQ = 0.7071;

juce::Font rulerFont()
{
    return uv::type::hint().withHeight (9.0f);
}

const char* const analyzerWords[] = { "Analyzer off", "Pre", "Post", "Pre + post" };
} // namespace

struct ResponseWell::Drag
{
    Hit hit;
    juce::Point<float> from;
    bool open = false;
    double fine = 1.0;
    bool alt = false;
    std::vector<int> params;
    double f0 = 0.0, g0 = 0.0, q0 = 0.0;
};

ResponseWell::ResponseWell (Model& m) : model (m), values ((size_t) param::count, 0.0)
{
    ni::ui::setInfo (*this, info::response);
    setWantsKeyboardFocus (true);
    setCaption ("RESPONSE");
    update();
}

ResponseWell::~ResponseWell() = default;

/* ------------------------------------------------------------ geometry -- */

juce::Rectangle<float> ResponseWell::plotArea() const
{
    const auto a = contentBounds();
    return { a.getX() + labelsLeft, a.getY(), a.getWidth() - labelsLeft, a.getHeight() - labelsUnder };
}

float ResponseWell::xOf (double hz) const
{
    const auto a = plotArea();
    const double t = std::log (juce::jlimit (lowestHz, highestHz, hz) / lowestHz) / std::log (highestHz / lowestHz);
    return a.getX() + (float) t * a.getWidth();
}

double ResponseWell::hzAt (float x) const
{
    const auto a = plotArea();
    const double t = juce::jlimit (0.0, 1.0, (double) ((x - a.getX()) / a.getWidth()));
    return lowestHz * std::pow (highestHz / lowestHz, t);
}

float ResponseWell::yOf (double db) const
{
    const auto a = plotArea();
    return a.getY() + (float) ((range - db) / (2.0 * range)) * a.getHeight();
}

double ResponseWell::dbAt (float y) const
{
    const auto a = plotArea();
    return range - 2.0 * range * (double) ((y - a.getY()) / a.getHeight());
}

void ResponseWell::cycleRange()
{
    range = range == 6 ? 12 : range == 12 ? 18 : range == 18 ? 30 : 6;
    rebuild();
    repaint();
}

/* ------------------------------------------------------------- values -- */

double ResponseWell::plain (int index) const
{
    auto& p = model.parameter (index);
    return (double) p.convertFrom0to1 (p.getValue());
}

void ResponseWell::set (int index, double value)
{
    auto& p = model.parameter (index);
    p.setValueNotifyingHost (p.convertTo0to1 ((float) value));
}

/* Each change one whole gesture: what a click, a key or the wheel is. */
void ResponseWell::edit (const std::vector<std::pair<int, double>>& changes)
{
    for (const auto& [index, value] : changes)
    {
        auto& p = model.parameter (index);
        p.beginChangeGesture();
        set (index, value);
        p.endChangeGesture();
    }
}

int ResponseWell::laneFor (int sel) const
{
    if (sel >= isolator)
        return lane::left;
    switch ((int) plain (param::node (sel, param::channel)))
    {
        case 2:  return lane::right;
        case 3:  return lane::mid;
        case 4:  return lane::side;
        default: return lane::left;
    }
}

void ResponseWell::setSelected (int sel)
{
    if (sel == selection)
        return;
    selection = sel;
    update();
    rebuild();
    repaint();
}

/* -------------------------------------------------------------- frame -- */

juce::String ResponseWell::captionText() const
{
    int heard = plain (param::isoOn) >= 0.5 ? 1 : 0;
    for (int s = 0; s < param::nodes; ++s)
        heard += (int) plain (param::node (s, param::type)) != type::empty && plain (param::node (s, param::on)) >= 0.5;
    auto text = juce::String ("RESPONSE   ") + juce::String (heard) + (heard == 1 ? " FILTER" : " FILTERS");
    if (shown.sampleRate > 0.0)
        text << "   " << juce::String (shown.sampleRate / 1000.0, shown.sampleRate == std::floor (shown.sampleRate / 1000.0) * 1000.0 ? 0 : 1) << " KHZ";
    static const char* const lanes[] = { "", "RIGHT", "MID", "SIDE" };
    if (shownLane != lane::left)
        text << "   " << lanes[shownLane];
    else if (selection < isolator && (int) plain (param::node (selection, param::channel)) == 1)
        text << "   LEFT";
    return text;
}

void ResponseWell::update()
{
    const int l = laneFor (selection);
    const auto& p = model.picture (l);
    std::vector<double> now ((size_t) param::count);
    for (int i = 0; i < param::count; ++i)
        now[(size_t) i] = plain (i);
    const bool same = hasPicture && l == shownLane && now == values && p.serial == shown.serial && p.total == shown.total
                      && p.node == shown.node && p.isoTotal == shown.isoTotal && p.analyzer == shown.analyzer
                      && p.pre == shown.pre && p.post == shown.post;
    if (same)
        return;
    shown = p;
    shownLane = l;
    values = std::move (now);
    hasPicture = true;
    setCaption (captionText());
    rebuild();
    repaint();
}

juce::Path ResponseWell::curvePath (const Picture::Curve& db) const
{
    const auto a = plotArea();
    juce::Path path;
    for (int i = 0; i < Picture::points; ++i)
    {
        const float x = xOf (shown.hz[(size_t) i]);
        const float y = juce::jlimit (a.getY() - 1.0f, a.getBottom() + 1.0f, yOf (db[(size_t) i]));
        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }
    return path;
}

juce::Path ResponseWell::areaPath (const Picture::Curve& db) const
{
    const auto a = plotArea();
    juce::Path path;
    path.startNewSubPath (a.getX(), a.getBottom());
    for (int i = 0; i < Picture::points; ++i)
    {
        const double t = (analyzerTop - db[(size_t) i]) / analyzerSpan;
        path.lineTo (xOf (shown.hz[(size_t) i]), a.getY() + (float) juce::jlimit (0.0, 1.0, t) * a.getHeight());
    }
    path.lineTo (a.getRight(), a.getBottom());
    path.closeSubPath();
    return path;
}

std::optional<juce::Point<float>> ResponseWell::handle (int slot) const
{
    /* Where the engine says it sits (fd_core::design::handle). */
    const double hz = shown.handleHz[(size_t) slot], db = shown.handleDb[(size_t) slot];
    if (! hasPicture || std::isnan (hz) || (int) plain (param::node (slot, param::type)) == type::empty)
        return std::nullopt;
    const auto a = plotArea();
    return juce::Point<float> (xOf (hz), juce::jlimit (a.getY(), a.getBottom(), yOf (db)));
}

bool ResponseWell::usesGain (int slot) const
{
    const int t = (int) plain (param::node (slot, param::type));
    return (model.typeUses (t, (int) plain (param::node (slot, param::order))) & uses::gain) != 0;
}

void ResponseWell::rebuild()
{
    ghosts.clear();
    own.clear();
    widthTicks.clear();
    if (! hasPicture)
        return;
    total = curvePath (shown.total);
    for (int s = 0; s < param::nodes; ++s)
    {
        const bool heard = (int) plain (param::node (s, param::type)) != type::empty && plain (param::node (s, param::on)) >= 0.5;
        if (heard && s != selection)
            ghosts.push_back (curvePath (shown.node[(size_t) s]));
    }
    const bool isoShown = plain (param::isoOn) >= 0.5 || selection == isolator;
    if (isoShown)
        for (const auto& band : shown.iso)
            ghosts.push_back (curvePath (band));
    if (selection == isolator)
        own = curvePath (shown.isoTotal);
    else if ((int) plain (param::node (selection, param::type)) != type::empty)
    {
        own = curvePath (shown.node[(size_t) selection]);
        if (const auto h = handle (selection))
            for (const double edge : shown.edgeHz[(size_t) selection])
                if (! std::isnan (edge))
                {
                    widthTicks.startNewSubPath (xOf (edge), h->y - 6.0f);
                    widthTicks.lineTo (xOf (edge), h->y + 6.0f);
                }
    }
    pre = areaPath (shown.pre);
    post = areaPath (shown.post);
    postEdge.clear();
    const auto a = plotArea();
    for (int i = 0; i < Picture::points; ++i)
    {
        const double t = (analyzerTop - shown.post[(size_t) i]) / analyzerSpan;
        const juce::Point<float> p (xOf (shown.hz[(size_t) i]), a.getY() + (float) juce::jlimit (0.0, 1.0, t) * a.getHeight());
        if (i == 0)
            postEdge.startNewSubPath (p);
        else
            postEdge.lineTo (p);
    }
}

/* ------------------------------------------------------------- paint -- */

void ResponseWell::paintPlot (juce::Graphics& g)
{
    const auto a = plotArea();
    const int mode = (int) plain (param::analyzer);
    /* Before the filters faded behind, after them highlighted in front. */
    if (shown.analyzer)
    {
        if (mode == 1 || mode == 3)
        {
            g.setColour (c::plotDry.withMultipliedAlpha (0.25f));
            g.fillPath (pre);
        }
        if (mode == 2 || mode == 3)
        {
            g.setColour (c::plotGhost);
            g.fillPath (post);
            g.setColour (c::inkDim);
            g.strokePath (postEdge, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
        }
    }

    /* THE RULERS: the frame's two edges, ticks and labels; no grid. */
    g.setColour (c::line200);
    g.drawLine (a.getX(), a.getY(), a.getX(), a.getBottom(), 1.0f);
    g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getBottom(), 1.0f);
    g.setFont (rulerFont());
    for (double decade = 10.0; decade <= 10000.0; decade *= 10.0)
        for (int k = 1; k <= 9; ++k)
        {
            const double hz = decade * k;
            if (hz > highestHz)
                continue;
            const bool named = (k == 1 || k == 2 || k == 5) && hz >= 20.0;
            const float x = xOf (hz);
            g.setColour (c::line200);
            g.drawLine (x, a.getBottom(), x, a.getBottom() + (named ? 3.0f : 2.0f), 1.0f);
            if (named)
            {
                const auto label = hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String ((int) hz);
                g.setColour (c::inkMuted);
                g.drawText (label, juce::Rectangle<float> (x - 16.0f, a.getBottom() + 3.0f, 32.0f, 11.0f), juce::Justification::centred);
            }
        }
    const int step = range == 6 ? 3 : range == 30 ? 10 : 6;
    for (int db = -range; db <= range; db += step)
    {
        const float y = yOf (db);
        g.setColour (c::line200);
        g.drawLine (a.getX() - 3.0f, y, a.getX(), y, 1.0f);
        g.setColour (c::inkMuted);
        /* Held inside the plot's height, so the top label stays out of the
         * caption's band. */
        const float top = juce::jlimit (a.getY(), a.getBottom() - 12.0f, y - 6.0f);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (a.getX() - 30.0f, top, 25.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    /* 0 dB: the signal untouched. */
    juce::Path zero;
    zero.startNewSubPath (a.getX(), yOf (0.0));
    zero.lineTo (a.getRight(), yOf (0.0));
    ni::ui::plot::dryLine (g, zero);

    g.saveState();
    g.reduceClipRegion (a.toNearestInt().expanded (0, 2));
    for (const auto& p : ghosts)
        ni::ui::plot::ghost (g, p);

    const bool isoShown = plain (param::isoOn) >= 0.5 || selection == isolator;
    if (isoShown)
    {
        const double lo = plain (param::isoLower), hi = std::max (plain (param::isoUpper), lo);
        for (const double f : { lo, hi })
        {
            ni::ui::plot::rule (g, xOf (f), a.getY(), a.getBottom());
            g.setColour (c::inkDim);
            g.fillRect (juce::Rectangle<float> (xOf (f) - 4.0f, a.getY(), 8.0f, 8.0f));
        }
    }

    if (! own.isEmpty())
    {
        g.setColour (c::bg000);
        g.strokePath (own, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));
        g.setColour (c::ink);
        g.strokePath (own, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
    }
    ni::ui::plot::curve (g, total);
    g.setColour (c::inkDim);
    g.strokePath (widthTicks, juce::PathStrokeType (1.0f));

    /* THE ONE AMBER MARK: where the rate clamps a heard filter's band. */
    for (int s = 0; s < param::nodes; ++s)
        if (shown.clamped[(size_t) s] && plain (param::node (s, param::on)) >= 0.5 && shown.sampleRate > 0.0)
        {
            ni::ui::plot::rule (g, xOf (0.49 * shown.sampleRate), a.getY(), a.getBottom(), true);
            break;
        }
    g.restoreState();

    /* THE HANDLES. */
    g.setFont (rulerFont());
    const auto dot = [&g] (juce::Point<float> p, bool selected, bool bypassed)
    {
        juce::Path d;
        d.addEllipse (p.x - handleR, p.y - handleR, 2.0f * handleR, 2.0f * handleR);
        if (selected && ! bypassed)
            uv::light::glowLed (g, d);
        g.setColour (bypassed ? c::bg000 : selected ? c::uv : c::ink);
        g.fillPath (d);
        g.setColour (bypassed ? c::inkDim : c::bg000);
        g.strokePath (d, juce::PathStrokeType (1.5f));
    };
    static const char* const channels[] = { "", " L", " R", " M", " S" };
    for (int s = 0; s < param::nodes; ++s)
        if (const auto h = handle (s))
        {
            const bool sel = s == selection;
            dot (*h, sel, plain (param::node (s, param::on)) < 0.5);
            g.setColour (sel ? c::ink : c::inkMuted);
            g.drawText (juce::String (s + 1) + channels[(int) plain (param::node (s, param::channel))],
                        juce::Rectangle<float> (h->x + 6.0f, h->y - 15.0f, 32.0f, 11.0f), juce::Justification::centredLeft);
        }
    if (isoShown)
    {
        const double lo = plain (param::isoLower), hi = std::max (plain (param::isoUpper), lo);
        const double middles[] = { std::sqrt (lowestHz * lo), std::sqrt (lo * hi), std::sqrt (hi * highestHz) };
        const int levels[] = { param::isoLow, param::isoMid, param::isoHigh };
        const int kills[] = { param::isoKillLow, param::isoKillMid, param::isoKillHigh };
        static const char* const letters[] = { "L", "M", "H" };
        for (int b = 0; b < 3; ++b)
        {
            const double level = plain (kills[b]) >= 0.5 || plain (levels[b]) <= -60.0 ? -1000.0 : plain (levels[b]);
            const juce::Point<float> p (xOf (middles[b]), juce::jlimit (a.getY(), a.getBottom(), yOf (level)));
            dot (p, selection == isolator, false);
            g.setColour (c::inkMuted);
            g.drawText (letters[b], juce::Rectangle<float> (p.x + 6.0f, p.y - 15.0f, 16.0f, 11.0f), juce::Justification::centredLeft);
        }
    }

    /* THE CAPTION'S RIGHT END: the analyzer's mode and the range, each a
     * click. */
    const auto band = juce::Rectangle<float> (0.0f, ni::ui::plot::inset - 3.0f, (float) getWidth() - ni::ui::plot::inset, ni::ui::plot::captionH);
    g.setFont (uv::type::hint());
    g.setColour (c::inkMuted);
    const auto rangeText = juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (range) + " dB";
    g.drawText (rangeText, band, juce::Justification::centredRight);
    const float rangeW = uv::type::width (uv::type::hint(), rangeText);
    g.drawText (juce::String (analyzerWords[juce::jlimit (0, 3, mode)]).toUpperCase(), band.withTrimmedRight (rangeW + 16.0f),
                juce::Justification::centredRight);
}

/* ------------------------------------------------------------ pointer -- */

std::optional<ResponseWell::Hit> ResponseWell::hitAt (juce::Point<float> p) const
{
    const auto a = plotArea();
    if (p.y < a.getY())
    {
        /* The caption's right end. */
        const auto rangeText = juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (range) + " dB";
        const float right = (float) getWidth() - ni::ui::plot::inset;
        const float rangeLeft = right - uv::type::width (uv::type::hint(), rangeText);
        if (p.x >= rangeLeft - 4.0f)
            return Hit { Hit::Kind::range, 0 };
        if (p.x >= rangeLeft - 120.0f)
            return Hit { Hit::Kind::analyzer, 0 };
        return std::nullopt;
    }
    std::optional<Hit> best;
    float bestD = reach;
    const auto consider = [&] (Hit h, juce::Point<float> at)
    {
        const float d = std::max (std::abs (at.x - p.x), std::abs (at.y - p.y));
        if (d < bestD)
        {
            bestD = d;
            best = h;
        }
    };
    if (selection < isolator)
        if (const auto h = handle (selection))
            for (const double edge : shown.edgeHz[(size_t) selection])
                if (! std::isnan (edge))
                    consider ({ Hit::Kind::width, selection }, { xOf (edge), h->y });
    for (int s = 0; s < param::nodes; ++s)
        if (const auto h = handle (s))
            consider ({ Hit::Kind::node, s }, *h);
    if (plain (param::isoOn) >= 0.5 || selection == isolator)
    {
        const double lo = plain (param::isoLower), hi = std::max (plain (param::isoUpper), lo);
        const double middles[] = { std::sqrt (lowestHz * lo), std::sqrt (lo * hi), std::sqrt (hi * highestHz) };
        const int levels[] = { param::isoLow, param::isoMid, param::isoHigh };
        for (int b = 0; b < 3; ++b)
            consider ({ Hit::Kind::band, b }, { xOf (middles[b]), juce::jlimit (a.getY(), a.getBottom(), yOf (plain (levels[b]))) });
        if (! best)
            for (int k = 0; k < 2; ++k)
                if (std::abs (xOf (k == 0 ? lo : hi) - p.x) < 4.0f)
                    return Hit { Hit::Kind::crossover, k };
    }
    return best;
}

void ResponseWell::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto hit = hitAt (e.position);
    if (! hit)
        return;
    switch (hit->kind)
    {
        case Hit::Kind::analyzer:
            /* Pre + post, Post, Pre, off, round again. */
            edit ({ { param::analyzer, (double) (((int) plain (param::analyzer) + 3) % 4) } });
            repaint();
            return;
        case Hit::Kind::range:
            cycleRange();
            return;
        case Hit::Kind::node:
        case Hit::Kind::width:
            if (onSelect)
                onSelect (hit->index);
            break;
        case Hit::Kind::crossover:
        case Hit::Kind::band:
            if (onSelect)
                onSelect (isolator);
            break;
    }
    drag = std::make_unique<Drag>();
    drag->hit = *hit;
    drag->from = e.position;
    drag->fine = e.mods.isShiftDown() ? 1.0 / fineRatio : 1.0;
    drag->alt = e.mods.isAltDown();
    if (hit->kind == Hit::Kind::node)
    {
        const int s = hit->index;
        drag->f0 = plain (param::node (s, param::freq));
        drag->g0 = plain (param::node (s, param::gain));
        drag->q0 = plain (param::node (s, param::q));
        drag->params = { param::node (s, param::freq),
                         usesGain (s) && ! drag->alt ? param::node (s, param::gain) : param::node (s, param::q) };
    }
    else if (hit->kind == Hit::Kind::width)
        drag->params = { param::node (hit->index, param::q) };
    else if (hit->kind == Hit::Kind::crossover)
        drag->params = { hit->index == 0 ? (int) param::isoLower : (int) param::isoUpper };
    else
    {
        const int levels[] = { param::isoLow, param::isoMid, param::isoHigh };
        drag->params = { levels[hit->index] };
        drag->g0 = std::max (-60.0, plain (levels[hit->index]));
    }
}

void ResponseWell::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == nullptr)
        return;
    const auto d = e.position - drag->from;
    if (! drag->open)
    {
        if (std::max (std::abs (d.x), std::abs (d.y)) < deadZone)
            return;
        for (const int p : drag->params)
            model.parameter (p).beginChangeGesture();
        drag->open = true;
    }
    const auto a = plotArea();
    const double fine = drag->fine;
    const double dbPerPx = 2.0 * range / a.getHeight();
    const double pxPerOctave = a.getWidth() / std::log2 (highestHz / lowestHz);
    switch (drag->hit.kind)
    {
        case Hit::Kind::node:
        {
            const int s = drag->hit.index;
            set (param::node (s, param::freq), juce::jlimit (10.0, 22000.0, drag->f0 * std::pow (2.0, fine * d.x / pxPerOctave)));
            if (drag->params[1] == param::node (s, param::gain))
            {
                const double sc = plain (param::scale) / 100.0;
                const double per = std::abs (sc) > 0.01 ? dbPerPx / sc : dbPerPx;
                set (param::node (s, param::gain), juce::jlimit (-30.0, 30.0, drag->g0 - fine * d.y * per));
            }
            else
                set (param::node (s, param::q), juce::jlimit (0.1, 40.0, drag->q0 * std::pow (2.0, -fine * d.y / 48.0)));
            break;
        }
        case Hit::Kind::width:
        {
            const int s = drag->hit.index;
            set (param::node (s, param::q), model.qForEdge (plain (param::node (s, param::freq)), hzAt (e.position.x)));
            break;
        }
        case Hit::Kind::crossover:
            if (drag->hit.index == 0)
                set (param::isoLower, juce::jlimit (30.0, 2000.0, hzAt (e.position.x)));
            else
                set (param::isoUpper, juce::jlimit (500.0, 16000.0, hzAt (e.position.x)));
            break;
        case Hit::Kind::band:
            set (drag->params[0], juce::jlimit (-60.0, 6.0, drag->g0 - fine * d.y * dbPerPx));
            break;
        case Hit::Kind::analyzer:
        case Hit::Kind::range:
            break;
    }
    update();
}

void ResponseWell::mouseUp (const juce::MouseEvent&)
{
    if (drag != nullptr && drag->open)
        for (const int p : drag->params)
            model.parameter (p).endChangeGesture();
    drag.reset();
}

void ResponseWell::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto hit = hitAt (e.position);
    if (hit && hit->kind == Hit::Kind::node)
    {
        const int s = hit->index;
        if (usesGain (s))
            edit ({ { param::node (s, param::gain), 0.0 } });
        else
            edit ({ { param::node (s, param::q), defaultQ } });
        return;
    }
    if (hit && hit->kind == Hit::Kind::band)
    {
        const int levels[] = { param::isoLow, param::isoMid, param::isoHigh };
        edit ({ { levels[hit->index], 0.0 } });
        return;
    }
    if (hit || ! plotArea().contains (e.position))
        return;
    /* A new filter, in the first empty slot. */
    for (int s = 0; s < param::nodes; ++s)
        if ((int) plain (param::node (s, param::type)) == type::empty)
        {
            const double f = hzAt (e.position.x);
            const int t = model.typeForNew (f);
            const double sc = plain (param::scale) / 100.0;
            const double gain = t == type::bell && std::abs (sc) > 0.01 ? juce::jlimit (-30.0, 30.0, dbAt (e.position.y) / sc) : 0.0;
            edit ({ { param::node (s, param::freq), f },
                    { param::node (s, param::gain), std::round (gain * 10.0) / 10.0 },
                    { param::node (s, param::q), defaultQ },
                    { param::node (s, param::order), 1.0 },
                    { param::node (s, param::on), 1.0 },
                    { param::node (s, param::type), (double) t } });
            if (onSelect)
                onSelect (s);
            return;
        }
}

void ResponseWell::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (selection >= isolator || (int) plain (param::node (selection, param::type)) == type::empty || wheel.deltaY == 0.0f)
        return;
    const double q = plain (param::node (selection, param::q));
    edit ({ { param::node (selection, param::q), juce::jlimit (0.1, 40.0, q * std::pow (2.0, (double) wheel.deltaY * 2.0)) } });
}

bool ResponseWell::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selection < isolator
        && (int) plain (param::node (selection, param::type)) != type::empty)
    {
        edit ({ { param::node (selection, param::type), (double) type::empty } });
        return true;
    }
    return false;
}

} // namespace ni::fd

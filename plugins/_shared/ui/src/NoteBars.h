// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Notes as a note editor draws them in its well: each a bar on its pitch row,
 * with the pitch the recording has drawn through it.
 *
 *   the bar      the note's span on its row, a row tall less a 1px gap above
 *                and below (6 to 24 px), radius-1 corners: bg-300 on a
 *                line-200 hairline; selected, bg-300 mixed with 35 % uv-deep
 *                on a uv hairline. Its shade says how loud the note is: the
 *                loudest note of all is opaque, a silent one 40 %
 *   the pitch    its contour, 1.5px, ink (selected: uv); a gap where it has
 *                no pitch
 *   the ghost    under an edited note, the contour as recorded, 1px line-200
 *                dashed 3 3
 *   muted        a note that is silent: its bar's outline alone -- no fill,
 *                no pitch, no ghost -- and still found, selected and handled
 *                as any other
 *   the handles  on a bar that has them (the selected note, under a tool
 *                that sets lengths), a 6px uv square at each end, on its row
 *   the marker   where a click would act on one note -- a split, an end's
 *                slide: a 1px uv line across its bar
 *   the marked   bars a drag is about to take as one -- a join: each on a uv
 *                hairline, and a 1px uv line from each to the next, centre to
 *                centre, so the run reads as the one note it will be
 *   the playhead a 1px uv-deep line the well's height
 *
 * A curve is sampled: value `i` sits at axis unit `start + i * step`, and a
 * NaN is a gap. The axes are the well's (ZoomMath.h): time across, pitch up.
 *
 * IT PAINTS AND ANSWERS WHAT IS WHERE; it takes no pointer. The editor that
 * owns it decides what a press means (noteAt, notesIn, edgeAt).
 */
#pragma once

#include "ZoomMath.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace ni::ui
{

class NoteBars : public juce::Component
{
public:
    struct Curve
    {
        double start = 0.0;
        double step = 1.0;
        std::vector<float> values;
    };

    struct Bar
    {
        std::uint64_t id = 0;
        double start = 0.0, end = 0.0;
        /* The pitch it is heard at, the row it sits on. */
        double centre = 60.0;
        Curve level;
        Curve contour;
        /* As recorded, for an edited note; empty otherwise. */
        Curve ghost;
        bool selected = false;
        /* Its edge handles are shown, and answer edgeAt(). */
        bool handles = false;
        /* One of a run a drag is about to take as one. */
        bool marked = false;
        /* Silent: drawn as its outline alone. */
        bool muted = false;
    };

    /* One end of a bar: its start, or its end. */
    struct Edge
    {
        std::uint64_t id = 0;
        bool end = false;
        bool operator== (const Edge& o) const noexcept { return id == o.id && end == o.end; }
    };

    /* A line across one bar, at a time. */
    struct Marker
    {
        std::uint64_t id = 0;
        double at = 0.0;
        bool operator== (const Marker& o) const noexcept { return id == o.id && juce::exactlyEqual (at, o.at); }
    };

    /* How far from its pitch a press still finds a note, pixels. */
    static constexpr float reach = 8.0f;
    /* A handle's side, and how far from its centre a press still finds it. */
    static constexpr float handleSize = 6.0f;
    static constexpr float handleReach = 5.0f;

    NoteBars();
    ~NoteBars() override;

    void setAxes (const zoom::Axis& time, const zoom::Axis& pitch);
    void setBars (std::vector<Bar>);
    const std::vector<Bar>& getBars() const noexcept { return bars; }
    void setPlayhead (std::optional<double> at);
    void setMarker (std::optional<Marker>);

    float xOf (double t) const noexcept;
    float yOf (double midi) const noexcept;
    double timeAt (float x) const noexcept;
    double pitchAt (float y) const noexcept;

    /* Half a bar's height, pixels. */
    float halfThickness() const noexcept;

    /* The note under a point, the topmost first. */
    std::optional<std::uint64_t> noteAt (juce::Point<float>) const;
    /* The notes a rectangle touches. */
    std::vector<std::uint64_t> notesIn (juce::Rectangle<float>) const;
    /* The handle under a point, of a bar that shows its handles. */
    std::optional<Edge> edgeAt (juce::Point<float>) const;
    /* Where a bar is drawn; empty for no such bar. */
    juce::Rectangle<float> boundsOf (std::uint64_t id) const;

    void paint (juce::Graphics&) override;

private:
    juce::Path line (const Curve&) const;
    juce::Rectangle<float> box (const Bar&) const;

    zoom::Axis time, pitch;
    std::vector<Bar> bars;
    /* Each bar's mean level over its span, over the loudest bar's: 0 to 1. */
    std::vector<float> loudness;
    std::optional<double> playhead;
    std::optional<Marker> marker;

    JUCE_DECLARE_NON_COPYABLE (NoteBars)
};

} // namespace ni::ui

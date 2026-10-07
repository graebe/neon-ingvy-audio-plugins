// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The plot primitives: the well a plot is drawn in, a millisecond ruler, the
 * min/max band a waveform is drawn with, and the marks the PlotWell card of
 * Ultraviolet 1.1.0 allows inside a well -- the web kit's Plot.jsx, natively.
 *
 * THE WELL. A bg-000 box on a line-100 hairline, its content inset 6px all
 * round, with a caption in the hint style, uppercase, ink-muted, top left in a
 * 14px band above the content. The caption names the picture and its span in
 * clauses three spaces apart ("SIGNAL   ONE CYCLE, 500 MS   DRY IN GREY,
 * GATED IN FRONT"). An editor's plot derives from PlotWell and draws in
 * paintPlot(); the caption is drawn over what it draws, as the card lays its
 * caption over its picture.
 *
 * DRAW WHAT THE ENGINE RENDERS. A plot is the engine's picture of the sound --
 * an envelope it rendered, a capture it took -- never a second model of the
 * DSP rebuilt here from the parameters: a plot that reasons about the sound is
 * a second implementation, and it drifts from the first. So nothing here
 * knows what a step, a tie or a slot is; the editor that has one draws it with
 * these.
 *
 * DATA COLOURS ONLY, and only as the card assigns them:
 *
 *   curve()   a 2px uv line with its arc glow       the curve
 *   under()   plot-fill                             the area under it
 *   ghost()   a 1px plot-ghost line                 a reference behind it
 *   dry()     plot-dry at half opacity              the input behind a trace
 *   wet()     uv with the arc glow                  the processed trace
 *   rule()    a line-200 rule, amber 2px when       a position mark; amber is
 *             something runs past it                the window's one amber mark
 *   Axis      a line-200 ruler with 3px ticks and hint labels about 38px apart
 *
 * THE GEOMETRY IS THE WEB EDITORS', in the well's own pixels, 1:1: the inset
 * and the caption band are measured from the well's outer edge (plot::inset,
 * plot::captionH), and a plot that lines up with a row of pads lines up the
 * same way it did. Nothing is stretched, so a caption keeps its shape.
 *
 * A well is solid to the Ground's rings (WaveSource.h). It takes the pointer
 * so the hint bar can say what the plot shows (setInfo on the well), and to
 * assistive technology it is an image named by its caption.
 */
#pragma once

#include "UvTokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace ni::ui
{

namespace plot
{

/* plot::inset and plot::captionH, named as the design names them. */
inline constexpr float inset = 6.0f;
inline constexpr float captionH = 14.0f;

/* The ruler's ticks hang this far below its line; its labels' baseline is
 * labelDrop below it. */
inline constexpr float tickLength = 3.0f;
inline constexpr float labelDrop = 12.0f;
/* The ladder picks the smallest interval whose ticks stay this far apart. */
inline constexpr float tickSpacing = 38.0f;

/* --------------------------------------------------------------- marks -- */

/* Each one draws in `g`'s coordinates, which are the well's. */
void curve (juce::Graphics&, const juce::Path& line);
void under (juce::Graphics&, const juce::Path& area);
void ghost (juce::Graphics&, const juce::Path& line);
void dry (juce::Graphics&, const juce::Path& area);
void wet (juce::Graphics&, const juce::Path& area);

/* A vertical position mark at `x`, from `top` to `bottom`. */
void rule (juce::Graphics&, float x, float top, float bottom, bool warn = false);

/* ---------------------------------------------------------------- band -- */

/*
 * A column-wise waveform capture, as the engines publish one: `count`
 * columns of `stride` floats each, in -1..1. Not owned.
 */
struct Capture
{
    const float* data = nullptr;
    int stride = 0;
    int count = 0;
};

/* Where a band is drawn: `width` whole pixels from x0, mid-way between top
 * and bottom. */
struct BandGeometry
{
    float x0 = 0.0f;
    int width = 0;
    float top = 0.0f;
    float bottom = 0.0f;
};

/*
 * ONE CLOSED MIN/MAX ENVELOPE for a capture, into `out` (cleared first).
 *
 * Walks the top edge forward and the bottom edge back, so the result is a
 * region to fill rather than two strokes. MIN AND MAX, NEVER A MEAN: a
 * transient is a fraction of a column, and averaging would report a signal
 * nobody is hearing. Each screen pixel takes the extremes of every capture
 * column behind it, which keeps a narrow dip visible at 256 columns in 700px.
 *
 * `seen`, when given, says which columns hold data yet: a column it rejects
 * is a gap that breaks the band into runs, each its own closed sub-path, so a
 * picture still filling reads as unfinished rather than as a signal that
 * stopped. A run of one pixel is not drawn.
 *
 * Nothing is allocated beyond `out`'s own storage, which a plot keeps from
 * frame to frame; fewer than two columns leave `out` empty.
 */
void band (juce::Path& out, const Capture&, int loIndex, int hiIndex, const BandGeometry&,
           const std::function<bool (int column)>& seen = {});

/* --------------------------------------------------------------- ruler -- */

/* One tick of the ruler: where, in ms, and its label. */
struct Tick
{
    double ms = 0.0;
    juce::String text;

    bool operator== (const Tick& o) const { return juce::exactlyEqual (ms, o.ms) && text == o.text; }
};

/*
 * The ticks of a ruler `widthPx` across the content (the well's width less
 * twice the inset) spanning `spanMs`. The landmark first -- `markMs` with its
 * unit, because it is exact and everything gives way to it -- then the
 * smallest interval of the ladder (1, 2, 5, 10 ... 5000 ms) whose ticks stay
 * tickSpacing apart, less any that would fall within 34px of the landmark.
 * `markMs` 0 for none. Empty for a span or a width it cannot draw.
 */
std::vector<Tick> axisTicks (float widthPx, double spanMs, double markMs = 0.0);

/*
 * A millisecond ruler along a well `wellWidth` wide, its line at `y`: the
 * line from inset to the far inset, a 3px tick and a centred hint label per
 * tick, the labels nudged in at the ends so none hangs outside the well.
 * Keeps its ticks, and works them out again only when its numbers change.
 */
class Axis
{
public:
    void set (float wellWidth, double spanMs, double markMs = 0.0);
    const std::vector<Tick>& ticks() const noexcept { return list; }

    /* The x of a time on the ruler. */
    float xOf (double ms) const;

    void paint (juce::Graphics&, float y) const;

private:
    float width = 0.0f;
    double span = 0.0, mark = 0.0;
    std::vector<Tick> list;
};

} // namespace plot

class PlotWell : public juce::Component
{
public:
    PlotWell();
    ~PlotWell() override;

    /* In capitals, as the card sets it; clauses three spaces apart. */
    void setCaption (const juce::String&);
    const juce::String& getCaption() const noexcept { return caption; }

    /* Where the picture goes: inset all round, the caption band off the top. */
    juce::Rectangle<float> contentBounds() const;

    /* The x of a fraction 0..1 of the content's width, clamped. */
    float xAt (float fraction) const;

    void paint (juce::Graphics&) final;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

protected:
    /* The picture, over the well and under its caption, in the well's own
     * pixels. */
    virtual void paintPlot (juce::Graphics&) {}

private:
    juce::String caption;

    JUCE_DECLARE_NON_COPYABLE (PlotWell)
};

} // namespace ni::ui

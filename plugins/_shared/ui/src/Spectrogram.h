// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The rolling picture of NI Spectrogram -- the web kit's Spectrogram.jsx,
 * natively. It draws columns of levels the analyzer measured; what a band, a
 * column or a slot MEANS (a frequency, a time, a bar) is the editor's, which
 * owns the axis the plugin sent.
 *
 * A RING, SO NOTHING MOVES. The history is an image `columns` wide and
 * `bands` tall, written as a ring: each column goes at a cursor that wraps,
 * and the visible picture is two copies -- the part after the cursor (the old
 * columns) and the part before it (the new ones). Shifting the picture left a
 * pixel per column would resample what the last shift resampled and smear the
 * old end of the picture within seconds. At the shipped size (606 x 256 into
 * 606 x 256) the copies are 1:1; anything else is left to the renderer's own
 * smoothing, vertically, rather than inventing detail the analysis lacks.
 *
 * TWO PICTURES, BOTH WRITTEN FROM EVERY COLUMN. The scrolling history, and a
 * SWEEP whose columns go where the caller says (`slots`) rather than where
 * they arrive -- which lets the x-axis be the host's bars instead of the
 * clock. Switching between them (setView) is a choice of what to show, never
 * a restart: the other picture was kept the whole time. In the sweep a jump
 * of a few slots is a gap, filled by interpolating the levels between the
 * two columns (a level byte is linear in dB, so a straight line in bytes is a
 * straight line on the picture's axis); a jump of more than half the width is
 * a seek and is not filled.
 *
 * THE COLOUR IS THE RAMP spec-0..spec-4, interpolated in sRGB into 256 levels
 * (ramp()), byte 0 being spec-0 exactly, so silence is the well it is drawn in
 * rather than the first step of a gradient. Band 0 is the lowest frequency
 * and is drawn at the bottom.
 *
 * THE LEVELS ARE KEPT AS NUMBERS beside the picture, because colour is lossy:
 * the crosshair reports what was measured (sample()), never a pixel read back
 * through the palette.
 *
 * THE CLASH is a reading of the picture, not part of it, so it is its own
 * layer composited over: a cell's intensity is its alpha (to 200 of 255, so
 * the partial underneath stays readable), and a cell on the region's edge --
 * a lit cell with an unlit neighbour above, below or one column back -- is
 * drawn at full alpha, which outlines the region. As built it is amber; the
 * 1.1.0 layouts' proposal SP4 draws it as ink hatching ("data, not a state,
 * so not amber"), and setClashStyle chooses.
 *
 * PAUSE IS A REPAINT GATE AND NOTHING MORE. Columns keep arriving and keep
 * being written behind the frozen picture, so resuming shows an up-to-date
 * view with the paused seconds in it. The frozen picture and its levels are
 * copied when the pause begins (and again if the view changes during it), so
 * the crosshair reads the moment on screen, not the ring rolling on behind it.
 *
 * NO ALLOCATION PER COLUMN. Every buffer is made when the band count is set
 * (in practice once; a new count means a new axis, and the history measured
 * against the old one is cleared). A batch writes into them and repaints
 * once, whatever its size, at whatever rate the plugin produces columns.
 *
 * THE CROSSHAIR answers the pointer and the keyboard alike, because 1.1.0
 * has every pointer control reachable by Tab and operable from the keys. Tab
 * onto the picture and the crosshair stands at its centre; the arrows move it
 * a pixel (Shift: ten), Home and End take it to the oldest and the newest
 * column (the left and right edges), Escape takes it away. Either way it is
 * one point, reported through onHover, so a readout cannot tell keys from a
 * pointer. A crosshair the keys placed leaves with the focus.
 *
 * Message thread. Draws the crosshair (a hairline each way in uv-deep at
 * 0.75), glow-focus round itself while it has visible keyboard focus, and the
 * sweep's playhead (uv at 0.9) in the bar view; none is part of the picture.
 * Solid to the Ground's rings.
 */
#pragma once

#include "Focus.h"
#include "Luminous.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace ni::ui
{

class Spectrogram final : public juce::Component,
                          public Luminous
{
public:
    /* The shipped picture: 606 columns (about 13 s at 47 a second) of 256
     * bands. */
    static constexpr int defaultColumns = 606;
    static constexpr int defaultBands = 256;

    /* What an arrow moves the crosshair, in pixels, and with Shift held. */
    static constexpr float keyStep = 1.0f;
    static constexpr float keyLeap = 10.0f;

    /* A clash cell below this level is outside the region altogether. */
    static constexpr std::uint8_t clashEdge = 12;
    /* The most a clash cell inside the region is lit, of 255. */
    static constexpr int clashCeiling = 200;

    enum class View
    {
        scroll,   // the history, newest on the right
        bars,     // the sweep, each column at its slot
    };

    enum class ClashStyle
    {
        amber,    // as built
        hatch,    // the 1.1.0 proposal SP4
    };

    explicit Spectrogram (int columns = defaultColumns, int bands = defaultBands);
    ~Spectrogram() override;

    /*
     * One batch of columns, oldest first, as the analyzer published them:
     * `count` columns of `bands` level bytes each, band 0 the lowest. `slots`
     * (one per column, 0..columns-1, or -1 for none) places them in the
     * sweep; `clash` (the same shape as `data`) is the clash measured for the
     * same columns. Neither is required. Nothing is kept past the call.
     */
    struct Batch
    {
        const std::uint8_t* data = nullptr;
        int count = 0;
        int bands = 0;
        const int* slots = nullptr;
        const std::uint8_t* clash = nullptr;
    };

    /* Writes a batch into both pictures, paused or not, and repaints once
     * unless paused. A batch with a different band count clears the history
     * first: it was measured against another axis. */
    void push (const Batch&);

    /* Every column gone, the picture back to spec-0: a new range, or a new
     * mix, makes everything in the history an answer about something else. */
    void clear();

    void setPaused (bool);
    bool isPaused() const noexcept { return paused; }

    void setView (View);
    View getView() const noexcept { return view; }

    void setClash (bool shown);
    bool isClashShown() const noexcept { return clashShown; }
    void setClashStyle (ClashStyle);
    ClashStyle getClashStyle() const noexcept { return clashStyle; }

    int getColumns() const noexcept { return columns; }
    int getBands() const noexcept { return bands; }

    /* Where the history writes next, which is also its left edge. */
    int getCursor() const noexcept { return cursor; }
    /* The sweep's last slot written, frozen with the picture; -1 for none. */
    int getPlayhead() const noexcept { return playhead; }

    /*
     * What is under the pointer, in the picture's own terms: a band index and
     * a level byte, never a frequency or a decibel. `age` is how many columns
     * left of the newest (the scrolling view's time), `slot` the column from
     * the left (the bar view's position); `x` and `y` are the pointer, in this
     * component's pixels.
     */
    struct Sample
    {
        float x = 0.0f, y = 0.0f;
        int band = 0;
        int age = 0;
        int slot = 0;
        int level = 0;
    };

    /* The sample at a point of this component, or none outside the picture or
     * before any band count. */
    std::optional<Sample> sampleAt (juce::Point<float>) const;
    /* The sample under the pointer, or none. */
    std::optional<Sample> hovered() const;

    /* Told whenever the sample under the pointer may have changed -- the
     * pointer moved, or the picture under it did -- and with none when it
     * leaves. */
    std::function<void (const std::optional<Sample>&)> onHover;

    /* The ramp: the colour of a level byte. */
    static juce::Colour ramp (std::uint8_t level);

    /* The level stored for a column of the CURRENT view's buffer -- the
     * history by ring position, the sweep by slot. For tests. */
    std::uint8_t levelAt (View, int column, int band) const;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    /* ---- Luminous: glow-focus round the picture */
    void paintLight (juce::Graphics&) override;

private:
    void setup (int bandCount);
    void writeHistory (const std::uint8_t* column, int at);
    void writeSweep (const std::uint8_t* column, int slot);
    void writeClash (const std::uint8_t* column, int historyAt, int slot);
    void freeze();
    void point (std::optional<juce::Point<float>>);
    /* The middle pixel, where the keys put a crosshair first. */
    juce::Point<float> centre() const;
    void report();
    void repaintCrosshair();
    juce::Rectangle<float> playheadBounds (int slot) const;

    const int columns;
    int bands = 0;

    /* The pictures and their clash layers, ring and sweep. */
    juce::Image history, sweep, clashHistory, clashSweep;
    /* The levels as measured, same geometry: [column * bands + band]. */
    std::vector<std::uint8_t> levels, sweepLevels;
    /* The last sweep column written, for the gap filler; the clash column
     * before this one, for the region's edge. */
    std::vector<std::uint8_t> headColumn, clashPrevious;
    bool hasClashPrevious = false;

    /* The frozen picture: x is x, oldest on the left. */
    juce::Image frozen, frozenClash;
    std::vector<std::uint8_t> frozenLevels;
    bool hasFrozen = false;

    int cursor = 0;
    int head = -1;
    int playhead = -1;

    bool paused = false;
    View view = View::scroll;
    bool clashShown = false;
    ClashStyle clashStyle = ClashStyle::amber;

    std::optional<juce::Point<float>> pointer;
    /* The crosshair is the keys', not the pointer's: it goes with the focus. */
    bool keyed = false;
    FocusVisibility focus { *this };
    juce::Image hatch;

    JUCE_DECLARE_NON_COPYABLE (Spectrogram)
};

} // namespace ni::ui

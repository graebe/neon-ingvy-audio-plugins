// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE RESPONSE: the window's one picture, 10 Hz to 22 kHz in equal steps of
 * pitch across, the dB range up (±6, ±12, ±18 or ±30, clicked in the caption).
 *
 * BACK TO FRONT, AND EACH ANSWERS A DIFFERENT QUESTION:
 *
 *   the analyzer   the spectrum before the filters (pre) faded behind, as
 *                  plot-dry at a quarter, and after them (post) in front of
 *                  it, plot-ghost under an ink-dim edge -- on its own scale,
 *                  -6 to -84 dBFS
 *   the rulers     frequency under, dB at the left; no grid lines
 *   0 dB           the grey line: the signal untouched
 *   the ghosts     every filter alone and the isolator's bands, plot-ghost
 *   the selected   that filter alone, ink on a bg-000 casing: what you asked
 *                  for (ink, not uv: ink over uv is 1.34:1)
 *   the total      uv with its glow: what you get, in the selected filter's
 *                  lane (the caption names Mid, Side, Left or Right)
 *   the handles    a dot per filter: ink, the selected one uv, a bypassed
 *                  one hollow; its slot's number beside it
 *
 * WHERE A HANDLE SITS: a Bell's, a shelf's and a tilt's at Freq and Gain; a
 * cut's on its curve at Freq; a band-pass's, a notch's and an all-pass's on
 * 0 dB. The selected Bell, Band-Pass or Notch shows its -3 dB edges as two
 * ticks. While the isolator shows, its crossovers are rules with a tab, its
 * bands' levels dots L, M and H.
 *
 * WHAT THE POINTER DOES. Drag a handle: Freq across, and Gain up (Q, for a
 * filter without gain, or with Alt held at the press); Shift at the press is
 * five times finer. Drag an edge tick for Q, a crossover for its frequency,
 * a band's dot for its level. The wheel over the selected filter is its Q.
 * Double-click a handle to reset its Gain (or Q); double-click anywhere else
 * to add a filter in the first empty slot -- a High-Pass below 30 Hz, a High
 * Cut above 15 kHz, a Bell between them at the pointer's gain. Delete
 * empties the selected filter. Every drag is one gesture on each parameter
 * it moves, so the host's undo and automation see one edit.
 *
 * THE ONE AMBER MARK: a rule where the sample rate clamps a filter's band.
 *
 * Everything drawn is the model's picture (fd_shell_read): the engine's
 * designs, never a second model of them. Message thread.
 */
#pragma once

#include "Model.h"
#include "Plot.h"

#include <functional>
#include <optional>
#include <vector>

namespace ni::fd
{

class ResponseWell final : public ni::ui::PlotWell
{
public:
    /* The selection the well and the row share: a slot, or the isolator. */
    static constexpr int isolator = param::nodes;

    static constexpr double lowestHz = 10.0, highestHz = 22000.0;
    /* The ruler's room: dB labels at the left, Hz labels under. */
    static constexpr float labelsLeft = 30.0f, labelsUnder = 14.0f;
    static constexpr float handleR = 4.5f;

    explicit ResponseWell (Model&);
    ~ResponseWell() override;

    /* The editor's frame: the picture read again, the paths rebuilt when it
     * moved. */
    void update();

    void setSelected (int selection);
    int selected() const noexcept { return selection; }
    std::function<void (int selection)> onSelect;

    /* ---- the picture's geometry, in the well's coordinates */
    juce::Rectangle<float> plotArea() const;
    float xOf (double hz) const;
    double hzAt (float x) const;
    float yOf (double db) const;
    double dbAt (float y) const;
    int rangeDb() const noexcept { return range; }
    void cycleRange();

    /* Where node `slot`'s handle is; none for an Empty slot. */
    std::optional<juce::Point<float>> handle (int slot) const;
    /* The lane the total is drawn in. */
    int lane() const noexcept { return shownLane; }

    /* The well's own double-click (the level range) is replaced. */
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void resized() override { rebuild(); }

private:
    struct Drag;
    struct Hit
    {
        enum class Kind { node, width, crossover, band, analyzer, range } kind;
        int index = 0;
    };

    void paintPlot (juce::Graphics&) override;
    void rebuild();
    std::optional<Hit> hitAt (juce::Point<float>) const;
    int laneFor (int selection) const;
    double plain (int index) const;
    /* Whether node `slot`'s vertical is its Gain (the engine's table says). */
    bool usesGain (int slot) const;
    void set (int index, double value);
    void edit (const std::vector<std::pair<int, double>>& changes);
    juce::Path curvePath (const Picture::Curve&) const;
    juce::Path areaPath (const Picture::Curve&) const;
    juce::String captionText() const;

    Model& model;
    int selection = 0;
    int range = 18;
    int shownLane = lane::left;
    Picture shown;
    std::vector<double> values;
    bool hasPicture = false;

    juce::Path total, own, pre, post, postEdge, widthTicks;
    std::vector<juce::Path> ghosts;
    std::unique_ptr<Drag> drag;

    JUCE_DECLARE_NON_COPYABLE (ResponseWell)
};

} // namespace ni::fd

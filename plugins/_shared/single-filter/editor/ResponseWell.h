// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE RESPONSE: the window's one picture -- the kit's FrequencyResponse with
 * one filter in it.
 *
 * OVER THE BACKDROP (the analyzer, the rulers, 0 dB: FrequencyResponse.h):
 *
 *   the curve      uv with its glow: the filter and Output, what its lane
 *                  hears -- the caption names the lane, which is the
 *                  filter's Channel (a Stereo filter's is the mid). An All
 *                  Pass, whose magnitude is flat, is drawn by its phase on
 *                  a ±180° scale at the right
 *   the edges      a Bell's, band-pass's or notch's -3 dB edges, two
 *                  ink-dim ticks; dragged, they set Q
 *   the handle     the kit's PlotHandle on the curve at Freq: sideways
 *                  Freq, up and down Gain for a Bell, a shelf and a Tilt
 *                  (Alt at the press: Q), Q for the rest
 *
 * THE ONE AMBER MARK: a rule where the sample rate clamps the filter's band.
 *
 * WHAT ELSE THE POINTER DOES: a click on the caption's right end sets the
 * analyzer's mode or the range; the wheel is Q. Every drag is one gesture on
 * each parameter it moves, so the host's undo and automation see one edit.
 *
 * Everything drawn is the model's picture (sf_shell_read): the engine's
 * design, never a second model of it. Message thread.
 */
#pragma once

#include "Model.h"

#include "FrequencyResponse.h"
#include "ParamBinding.h"
#include "PlotHandle.h"

#include <memory>
#include <optional>

namespace ni::sf
{

class ResponseWell final : public ni::ui::FrequencyResponse
{
public:
    explicit ResponseWell (Model&);
    ~ResponseWell() override;

    /* The editor's frame: the picture read again, the paths rebuilt when it
     * moved. */
    void update();

    /* What the tests reach. */
    ni::ui::PlotHandle& handle() noexcept { return *dot; }
    const Picture& shownPicture() const noexcept { return shown; }
    bool drawsPhase() const noexcept { return hasPhaseScale(); }
    /* Where the handle's centre is, in the well's coordinates. */
    juce::Point<float> handleAt() const;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void paintPlot (juce::Graphics&) override;
    void geometryChanged() override;
    void rebuild();
    double plain (int field) const;
    juce::String captionText() const;
    bool nearEdge (juce::Point<float>) const;

    Model& model;
    const int qIndex, gainIndex;
    ni::ui::ParamBinding freq;
    std::unique_ptr<ni::ui::ParamBinding> vertical, alt;
    std::unique_ptr<ni::ui::PlotHandle> dot;

    Picture shown;
    std::array<double, 16> values {};
    bool hasPicture = false;
    juce::Path line, widthTicks;
    /* Dragging an edge: Q, one gesture. */
    bool draggingEdge = false;

    JUCE_DECLARE_NON_COPYABLE (ResponseWell)
};

} // namespace ni::sf

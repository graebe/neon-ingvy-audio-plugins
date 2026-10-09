// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's window, 840 x 532 at its design size, on Ultraviolet: the
 * board "NI Bit-Crusher" on the NI Plugin Layouts canvas
 * (design/designs/BitCrusher.dc.html), built from the kit.
 *
 * TWO PICTURES OVER THREE PANELS, on one grid of three 248px columns 16 apart:
 *
 *    (32, 32)    STEPS, one column: the staircase, input across, output up
 *    (296, 32)   SIGNAL, two columns: the last ~11 ms, input and output
 *    (32, 256)   three standard panels, a column each, 208 tall:
 *
 *                  STEPS   Bits, Shape, Dither        Mid-Rise, Anti-Alias
 *                  HOLD    Rate, Jitter, Octave       Pre-Filter, Post-Filter
 *                  LEVEL   Drive, Mix, Output
 *
 *    (0, 488)    the Hint bar: three conventions, Motion, the Signature
 *
 * The staircase sits over the panel that shapes it. A crusher does three
 * things -- it keeps fewer levels, it keeps them less often, and it is
 * driven into them and mixed back -- so there are three panels of three
 * knobs, each with the switches that change how that panel's knobs act.
 * Drive is a level, not a step: it decides how far up the staircase a signal
 * reaches, and lives with Mix and Output.
 *
 * STATE LIVES IN THE MODEL AND THE CONTROLS. tick() reads the model's
 * pictures and repaints what moved; the editor's one FrameClock calls it on
 * the display's frames. Every control is a host parameter, bound through
 * ParamBinding; the window keeps nothing a session would miss. Message thread
 * only.
 */
#pragma once

#include "Model.h"
#include "Plots.h"

#include "EditorFrame.h"
#include "Fit.h"
#include "Panel.h"
#include "ParamControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace ni::bc
{

class Editor final : public juce::Component
{
public:
    static constexpr int designWidth = 840;
    static constexpr int designHeight = 532;

    /* The grid, in the content's own pixels (inside the window padding). */
    static constexpr int column = 248;
    static constexpr int gutter = 16;
    static constexpr int plotH = 200;
    static constexpr int panelsY = plotH + 24;
    static constexpr int panelH = 208;

    /* `model` must outlive the editor; `clock` is the frame's (EditorFrame). */
    explicit Editor (Model&, ni::ui::EditorFrame::Clock clock = {});
    ~Editor() override;

    /* One frame: the model's pictures into the wells. The FrameClock's
     * callback, public so a test can run frames on its own clock. */
    void tick (double nowMs);

    /* ---- what the tests reach */
    ni::ui::EditorFrame& frame() noexcept { return window; }
    ni::ui::FixedDesign& fit() noexcept { return scaler; }
    juce::Component& content() noexcept;
    StepsPlot& stepsPlot() noexcept { return *steps; }
    SignalPlot& signalPlot() noexcept { return *signal; }
    ni::ui::Panel& panel (int index) noexcept { return *panels[(size_t) index]; }
    ni::ui::ParamKnob& knob (int parameterIndex);
    ni::ui::ParamToggle& toggle (int parameterIndex);

    void resized() override;

private:
    class View;

    void layout();

    Model& model;
    ni::ui::EditorFrame window;
    std::unique_ptr<View> view;
    std::unique_ptr<StepsPlot> steps;
    std::unique_ptr<SignalPlot> signal;
    std::array<std::unique_ptr<ni::ui::Panel>, 3> panels;
    std::vector<std::unique_ptr<ni::ui::ParamKnob>> knobs;
    std::vector<std::unique_ptr<ni::ui::ParamToggle>> toggles;

    ni::ui::FixedDesign scaler { window, designWidth, designHeight };
    ni::ui::FrameClock::Subscription frames;

    JUCE_DECLARE_NON_COPYABLE (Editor)
};

} // namespace ni::bc

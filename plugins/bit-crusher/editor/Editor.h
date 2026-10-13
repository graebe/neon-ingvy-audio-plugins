// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's window, 1168 x 532 at its design size, on Ultraviolet: the
 * board "NI Bit-Crusher — proposed" on the NI Plugin Layouts canvas
 * (design/designs/BitCrusher-Proposed.dc.html), built from the kit.
 *
 * FOUR PICTURES OVER FOUR PANELS, on one grid of four 264px columns 16
 * apart -- as wide as a panel must be for its two switches, labels and all --
 * each picture over the panel that shapes it, in the order of the signal:
 *
 *    (32, 32)    AMPLITUDE CRUSH: the staircase, input across, output up
 *    (312, 32)   TIME CRUSH: the hold's last sixteen periods
 *    (592, 32)   SIGNAL: the last ~11 ms, input and output
 *    (872, 32)   FILTER: its response, frequency across, gain up
 *    (32, 256)   four standard panels, a column each, 208 tall:
 *
 *                  AMPLITUDE CRUSH  Bits, Shape, Dither    Mid-Rise, Anti-Alias
 *                  TIME CRUSH       Rate, Jitter, Octave   Pre, Post
 *                  LEVEL            Drive, Mix, Output     Refresh, Pause
 *                  FILTER           Cutoff, Peak, Slope    Filter (on/off)
 *
 * The Filter acts on the crushed signal before the Mix (bc-core), and is off
 * by default.
 *
 *    (0, 488)    the Hint bar: three conventions, the Signature
 *
 * A crusher does three things -- it makes the amplitude discrete (keeps fewer
 * levels), it makes time discrete (keeps them less often), and it is driven
 * into them and mixed back -- so there are three panels of three knobs, each
 * with the switches that change how that panel's knobs act. Drive is a level,
 * not a step: it decides how far up the staircase a signal reaches, and lives
 * with Mix and Output. The filter switches read Pre and Post -- before and
 * after the hold -- because the host's names do not both fit a column.
 *
 * WHAT THE PICTURES SHOW, AND HOW OFTEN. Refresh and Pause sit in the Level
 * panel's free row, under the Signal: they are the window's, not the sound's.
 * tick() takes a new look from the model at most once a Refresh period, and
 * none while paused, and hands that one look to all three wells, so they show
 * one moment. Pause is the window's alone and not saved; Refresh is a host
 * parameter saved with the set and never automated.
 *
 * A LOOK IS SHOWN ON THE SETTINGS THAT MADE IT. The staircase follows the
 * knobs every frame; a look's samples were made with whatever the knobs were
 * then. So the two discretizations' live views are drawn only while the look
 * is current (current()): made with the settings the knobs hold now, and with
 * them throughout. Turn Bits and the dots go; unpaused, the first current look
 * is taken the moment there is one, whatever Refresh says, so they come back
 * on the new staircase a few dozen milliseconds later. Paused, they stay away
 * until the knobs are back or the pause ends. The Signal is the sound, and
 * shows any look.
 *
 * STATE LIVES IN THE MODEL AND THE CONTROLS. tick() reads the model's
 * pictures and repaints what moved; the editor's one FrameClock calls it on
 * the display's frames. Every control but Pause is a host parameter, bound
 * through ParamBinding; the window keeps nothing a session would miss.
 * Message thread only.
 */
#pragma once

#include "Model.h"
#include "Plots.h"

#include "Button.h"
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
    static constexpr int designWidth = 1168;
    static constexpr int designHeight = 532;

    /* The grid, in the content's own pixels (inside the window padding). */
    static constexpr int column = 264;
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

    /* Hold the three pictures still, or let them go. */
    void setPaused (bool);
    bool isPaused() const noexcept { return paused; }

    /* Whether `look` was made with the settings the knobs hold now, all of
     * it (Model.h: settings, settled). */
    bool current (const Scope& candidate) const;

    /* ---- what the tests reach */
    ni::ui::EditorFrame& frame() noexcept { return window; }
    ni::ui::FixedDesign& fit() noexcept { return scaler; }
    juce::Component& content() noexcept;
    AmplitudePlot& amplitudePlot() noexcept { return *amplitude; }
    TimePlot& timePlot() noexcept { return *timeCrush; }
    FilterPlot& filterPlot() noexcept { return *filterResponse; }
    SignalPlot& signalPlot() noexcept { return *signal; }
    ni::ui::ParamSelect& refreshSelect() noexcept { return *refresh; }
    ni::ui::Button& pauseButton() noexcept { return pause; }
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
    std::unique_ptr<AmplitudePlot> amplitude;
    std::unique_ptr<TimePlot> timeCrush;
    std::unique_ptr<SignalPlot> signal;
    std::unique_ptr<FilterPlot> filterResponse;
    std::array<std::unique_ptr<ni::ui::Panel>, 4> panels;
    std::vector<std::unique_ptr<ni::ui::ParamKnob>> knobs;
    std::vector<std::unique_ptr<ni::ui::ParamToggle>> toggles;
    std::unique_ptr<ni::ui::ParamSelect> refresh;
    ni::ui::Button pause { {}, "pause" };

    /* The look the wells show, and when it was taken. */
    Scope look;
    double lookedAt = -1.0e300;
    bool paused = false;

    ni::ui::FixedDesign scaler { window, designWidth, designHeight };
    ni::ui::FrameClock::Subscription frames;

    JUCE_DECLARE_NON_COPYABLE (Editor)
};

} // namespace ni::bc

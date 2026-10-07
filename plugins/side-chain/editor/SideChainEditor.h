// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's editor: the web editor's App.jsx, natively, on Ultraviolet
 * 1.1.0, laid out as the layout canvas's "NI Side-Chain -- as built" artboard
 * (design/designs/SideChain.dc.html), 760 x 604.
 *
 * ONE ALIGNMENT IS LOAD-BEARING: the shape you drag sits directly above the
 * audio it shaped, in one well spanning exactly one cycle (Shaper.h). That is
 * the whole editor; the knobs and selects below it are the same parameters as
 * numbers. There is no panel: "a window with a single function has no panel
 * at all", so the knobs sit on the window.
 *
 * TOP TO BOTTOM, inside the window padding (EditorFrame), on the 4px grid:
 *
 *    16   the header: the trigger's state, and at most one amber warning
 *     8
 *   260   the plot
 *    24
 *   106   the knobs: Depth, Delay, Attack, Hold, Release, then the source's
 *         own -- Note, Ch and Vel for MIDI, Thresh and Lockout for Sidechain
 *    20
 *    28   the trigger's row: Src, then Rate on Cycle or the Gate switch on MIDI
 *     8
 *    28   the shape's row: Curve and the "% of cycle" switch
 *
 * THE SOURCE'S OWN CONTROLS, AND ONLY THE ONES THAT APPLY: a Threshold on a
 * tempo-locked duck invites turning it and concluding the plugin is broken.
 * The two rows are split by subject -- the trigger, then the shape every
 * source has -- so the shape's controls do not move when the source changes.
 *
 * WHAT 1.1.0 CHANGED HERE, against the web editor: Delay draws its arc from 12
 * o'clock (Knob card); Note (128 options) and Channel (17) are stepped knobs,
 * because a Select holds twelve at most (Select card); Mode and Time, two
 * options each, are switches ("two options are a Toggle"); the plot's caption
 * names its span ("ONE CYCLE, 500 MS", PlotWell card); the input behind the
 * output is plot-dry and the measured envelope plot-ghost, the data colours;
 * and the hint bar is the kit's, with the Motion switch and every control's
 * info line, stating conventions only (Hint card) -- so the stage times'
 * reading, which the web bar carried as "225 ms total", moved into the stage
 * knobs' readouts, which Time now switches between ms and % of the cycle, as
 * the canvas's proposed artboard draws them.
 *
 * STATE LIVES IN THE MODEL AND THE CONTROLS. tick() reads the model's
 * snapshots and repaints; the editor's one FrameClock calls it on the display's
 * frames. A frame that never comes leaves the picture late, never wrong.
 * Message thread only.
 */
#pragma once

#include "Model.h"
#include "Shaper.h"
#include "Status.h"

#include "EditorFrame.h"
#include "Fit.h"
#include "Luminous.h"
#include "ParamChoiceKnob.h"
#include "ParamControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace ni::sc
{

class SideChainEditor final : public juce::Component
{
public:
    static constexpr int designWidth = 760;
    static constexpr int designHeight = 604;

    /* The content's rows, from the top of the content. */
    static constexpr int headerH = 16;
    static constexpr int plotY = headerH + 8;
    static constexpr int plotH = 260;
    static constexpr int knobsY = plotY + plotH + 24;
    static constexpr int triggerRowY = knobsY + ni::ui::Knob::cardHeight + 20;
    static constexpr int shapeRowY = triggerRowY + 28 + 8;

    /* `model` must outlive the editor; `clock` is the frame's (EditorFrame). */
    explicit SideChainEditor (Model&, ni::ui::EditorFrame::Clock clock = {});
    ~SideChainEditor() override;

    /* One frame: the model's snapshots into the picture. The FrameClock's
     * callback, public so a test can run frames on its own clock. */
    void tick (double nowMs);

    /* ---- what the tests reach */
    ni::ui::EditorFrame& frame() noexcept { return window; }
    ni::ui::FixedDesign& fit() noexcept { return scaler; }
    Shaper& shaper() noexcept { return *plot; }
    juce::Component& content() noexcept;

    /* The header's two texts, as drawn (the header sets them in capitals). */
    const juce::String& stateText() const noexcept;
    const juce::String& warningText() const noexcept;

    ni::ui::ParamKnob& knob (int parameterIndex);
    ni::ui::ParamSelect& select (int parameterIndex);
    ni::ui::ParamToggle& toggle (int parameterIndex);

    void resized() override;

private:
    class Header;
    class View;

    void showSource();
    void layoutRows();
    /* The stages' readouts read again, in the unit Time asks for. */
    void refreshStages();
    bool stagesInMs() const;

    Model& model;
    ni::ui::EditorFrame window;
    std::unique_ptr<View> view;
    std::unique_ptr<Header> header;
    std::unique_ptr<Shaper> plot;

    /* Every control, by its parameter. */
    std::vector<std::unique_ptr<ni::ui::ParamKnob>> knobs;
    std::unique_ptr<ni::ui::ParamSelect> sourceSelect, rateSelect, curveSelect;
    std::unique_ptr<ni::ui::ParamToggle> gateSwitch, percentSwitch;

    /* What says which source's controls show, and the hint's unit. */
    ni::ui::ParamBinding sourceParam, timeParam;

    /* The latest snapshots, which the stages' readouts read in ms. */
    State lastState;
    StageMs lastStages;

    TriggerWatch watch;
    ni::ui::FixedDesign scaler { window, designWidth, designHeight };
    ni::ui::FrameClock::Subscription frames;

    JUCE_DECLARE_NON_COPYABLE (SideChainEditor)
};

} // namespace ni::sc

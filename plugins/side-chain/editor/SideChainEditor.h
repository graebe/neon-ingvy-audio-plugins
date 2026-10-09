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
 *         own -- Note, Channel and Velocity for MIDI, Threshold and Lockout
 *         for Sidechain -- sharing the row's width equally, as the web row's
 *         flex cards did: 120px each on Cycle, the artboard's measure
 *    18
 *    28   the trigger's row: Source, then Rate on Cycle or the Gate switch on
 *         MIDI
 *     8
 *    28   the shape's row: Curve and the "% of cycle" switch, and at its
 *         right end the Kick the plot draws behind the duck: off, the
 *         sidechain key (while the host routes one), or a live Listen-In bus
 *
 * The rows land where the web editor and the artboard put them, at 464 and
 * 500 in the window, both on the grid. The web's knob card was 104 tall with
 * 20 under it; the kit's card is 106 (its label band is 14, not 11), so the
 * gap under it is 18 and the 124 between the knobs' top and the trigger's row
 * stays the web's.
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
 * names the picture and its span ("SHAPE   ONE CYCLE, 500 MS", PlotWell
 * card); the input behind the output is plot-dry and the measured envelope
 * plot-ghost, the data colours; the playhead is the card's line-200 rule; and
 * the hint bar is the kit's, with the Motion switch and every control's info
 * line, stating conventions only (Hint card) -- so the stage times' reading,
 * which the web bar carried as "225 ms total", moved: into the stage knobs'
 * readouts, which Time now switches between ms and % of the cycle, as the
 * canvas's proposed artboard draws them, and, as a total, into the caption
 * ("STAGES 225 MS"), where the canvas's SC1 puts it. And the labels are
 * words, Source, Channel, Velocity and Threshold, not the web's Src, Ch, Vel
 * and Thresh (canvas S8): they are what the manual calls the controls, and
 * what a screen reader says.
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
#include "Select.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
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
    static constexpr int triggerRowY = knobsY + ni::ui::Knob::cardHeight + 18;
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
    /* The Kick picker, and the choice each of its options stands for
     * (Model.h: kickOff, kickKey, a bus's slot). */
    ni::ui::Select& kickPicker() noexcept { return *kickSelect; }
    const std::vector<int>& kickChoices() const noexcept { return kickOptions; }

    void resized() override;

private:
    class Header;
    class View;

    void showSource();
    void layoutRows();
    /* The Kick picker's options and choice, from the kick the plot read. */
    void refreshKick();
    /* The stages' readouts read again, in the unit Time asks for. */
    void refreshStages();
    bool stagesInMs() const;
    /* A stage's text in that unit, and typing read into its parameter's
     * normalised value -- the knobs' and the handles' both. */
    juce::String stageReadout (int parameterIndex);
    std::optional<float> stageTyped (int parameterIndex, const juce::String& typed);

    Model& model;
    ni::ui::EditorFrame window;
    std::unique_ptr<View> view;
    std::unique_ptr<Header> header;
    std::unique_ptr<Shaper> plot;

    /* Every control, by its parameter. */
    std::vector<std::unique_ptr<ni::ui::ParamKnob>> knobs;
    std::unique_ptr<ni::ui::ParamSelect> sourceSelect, rateSelect, curveSelect;
    std::unique_ptr<ni::ui::ParamToggle> gateSwitch, percentSwitch;
    std::unique_ptr<ni::ui::Select> kickSelect;
    std::vector<int> kickOptions;

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

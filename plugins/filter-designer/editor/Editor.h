// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Filter Designer's window, 888 x 708 at its design size, on Ultraviolet:
 * the board "NI Filter Designer — proposed" on the NI Plugin Layouts canvas
 * (design/designs/FilterDesigner-Proposed.dc.html), built from the kit.
 *
 *    (0, 0)      RESPONSE, the whole width: ResponseWell.h
 *    (0, 312)    the node row: sixteen cells and ISO (NodeRow.h)
 *    (0, 376)    INSPECTOR, two columns: the selected filter -- Type, Family,
 *                Order, Channel; Freq, Gain, Q (Resonance on a cut), Ripple,
 *                Stopband -- or the isolator -- Low, Mid, High, Low-Mid, Mid-High,
 *                Slope, a Kill under each band. On at the title's right.
 *    (560, 376)  OUTPUT: Output, Scale; Adaptive, Bypass
 *    (0, 640)    the Hint bar: three conventions, Motion, the Signature
 *
 * WHAT A TYPE DOES NOT USE IS DISABLED, NEVER HIDDEN, so nothing in the
 * window moves when the type changes; which it is, is the engine's
 * (fd_type_uses, fd_family_uses_*). The inspector's controls are bound to
 * the selected slot's parameters and made again when the selection moves.
 *
 * STATE LIVES IN THE MODEL AND THE CONTROLS. tick() reads the model and
 * repaints what moved; the editor's FrameClock calls it on the display's
 * frames. Which filter is selected, and the picture's range, are the
 * window's own. Message thread only.
 */
#pragma once

#include "Model.h"
#include "NodeRow.h"
#include "ResponseWell.h"

#include "EditorFrame.h"
#include "Fit.h"
#include "Panel.h"
#include "ParamChoiceKnob.h"
#include "ParamControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace ni::fd
{

class Editor final : public juce::Component
{
public:
    static constexpr int designWidth = 888;
    static constexpr int designHeight = 708;

    /* The grid, in the content's own pixels (inside the window padding). */
    static constexpr int wellH = 288;
    static constexpr int rowY = wellH + 24;
    static constexpr int panelsY = rowY + NodeRow::cell + 24;
    static constexpr int panelH = 232;
    static constexpr int inspectorW = 544;
    static constexpr int outputX = inspectorW + 16;
    static constexpr int outputW = 264;

    /* `model` must outlive the editor; `clock` is the frame's (EditorFrame). */
    explicit Editor (Model&, ni::ui::EditorFrame::Clock clock = {});
    ~Editor() override;

    /* One frame: the picture and the controls' states. Public so a test can
     * run frames on its own clock. */
    void tick (double nowMs);

    /* Select a slot (0..15) or the isolator (ResponseWell::isolator). */
    void select (int selection);
    int selected() const noexcept { return selection; }

    /* ---- what the tests reach */
    ni::ui::EditorFrame& frame() noexcept { return window; }
    ni::ui::FixedDesign& fit() noexcept { return scaler; }
    juce::Component& content() noexcept;
    ResponseWell& well() noexcept { return *response; }
    NodeRow& row() noexcept { return *nodes; }
    ni::ui::Panel& inspector() noexcept { return *inspectorPanel; }
    ni::ui::Panel& outputPanel() noexcept { return *outPanel; }
    /* The inspector's knobs and selects, by parameter index; null when the
     * inspector shows none for it. */
    ni::ui::ParamKnob* knobFor (int parameterIndex);
    ni::ui::ParamSelect* selectFor (int parameterIndex);
    /* A switch by parameter index; -1 is the Bypass, the shell's own. */
    ni::ui::ParamToggle* toggleFor (int parameterIndex);

    void resized() override;

private:
    class View;
    class Caption;

    void layout();
    void rebuildInspector();
    void refreshInspector();

    Model& model;
    ni::ui::EditorFrame window;
    std::unique_ptr<View> view;
    std::unique_ptr<ResponseWell> response;
    std::unique_ptr<NodeRow> nodes;
    std::unique_ptr<ni::ui::Panel> inspectorPanel, outPanel;

    /* The inspector's controls, for the selection now. */
    std::vector<std::unique_ptr<ni::ui::ParamKnob>> knobs;
    std::vector<std::unique_ptr<ni::ui::ParamSelect>> selects;
    std::vector<std::unique_ptr<Caption>> captions;
    std::vector<std::unique_ptr<ni::ui::ParamToggle>> toggles;
    /* Output's, made once. */
    std::vector<std::unique_ptr<ni::ui::ParamKnob>> outKnobs;
    std::unique_ptr<ni::ui::ParamToggle> adaptive, bypassSwitch;

    int selection = 0;
    /* What the inspector last showed for, so it is refreshed only when that
     * moved: type, order, family. */
    std::array<int, 3> shownFor { -1, -1, -1 };

    ni::ui::FixedDesign scaler { window, designWidth, designHeight };
    ni::ui::FrameClock::Subscription frames;

    JUCE_DECLARE_NON_COPYABLE (Editor)
};

} // namespace ni::fd

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A single filter's window, 664 x 572 at its design size, on Ultraviolet: the
 * board "NI Single Filter — proposed" on the NI Plugin Layouts canvas
 * (design/designs/SingleFilter-Proposed.dc.html), built from the kit -- one
 * window for the nine products, each showing only what its type has.
 *
 *    (0, 0)     RESPONSE, the whole width: ResponseWell.h
 *    (0, 256)   FILTER: the type as its title, Bypass at its right and the
 *               type's switch before it -- Matched (High/Low Pass), Adaptive
 *               (Bell); the selects -- Family, Slope (an All Pass's Order),
 *               Channel; the knobs in Filter Designer's order -- Freq, Gain,
 *               Q, Ripple, Stopband -- and Output always in the last column,
 *               so the level sits in the same place in all nine
 *    (0, 496)   the Hint bar: the well's conventions, the Signature
 *
 * WHAT THE TYPE HAS BUT DOES NOT USE RIGHT NOW IS DISABLED, NEVER HIDDEN (Q
 * at 6 dB/oct; Ripple and Stopband per family): which it is, is the
 * engine's (sf_uses). What the type does not have is not there at all.
 *
 * STATE LIVES IN THE MODEL AND THE CONTROLS. tick() reads the model and
 * repaints what moved; the editor's FrameClock calls it on the display's
 * frames. Message thread only.
 */
#pragma once

#include "Model.h"
#include "ResponseWell.h"

#include "EditorFrame.h"
#include "Fit.h"
#include "Panel.h"
#include "ParamControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace ni::sf
{

class Editor final : public juce::Component
{
public:
    static constexpr int designWidth = 664;
    static constexpr int designHeight = 572;

    /* The grid, in the content's own pixels (inside the window padding). */
    static constexpr int wellH = 232;
    static constexpr int panelY = wellH + 24;
    static constexpr int panelH = 216;
    static constexpr int columns = 6;

    /* `model` must outlive the editor; `clock` is the frame's (EditorFrame). */
    explicit Editor (Model&, ni::ui::EditorFrame::Clock clock = {});
    ~Editor() override;

    /* One frame: the picture and the controls' states. Public so a test can
     * run frames on its own clock. */
    void tick (double nowMs);

    /* ---- what the tests reach */
    ni::ui::EditorFrame& frame() noexcept { return window; }
    ni::ui::FixedDesign& fit() noexcept { return scaler; }
    juce::Component& content() noexcept;
    ResponseWell& well() noexcept { return *response; }
    ni::ui::Panel& panel() noexcept { return *filterPanel; }
    /* The controls, by field (field::*); null for one the product lacks. */
    ni::ui::ParamKnob* knobFor (int field);
    ni::ui::ParamSelect* selectFor (int field);
    ni::ui::ParamToggle* toggleFor (int field);
    ni::ui::ParamToggle& bypass() noexcept { return *bypassSwitch; }

    void resized() override;

private:
    class View;
    class Caption;

    struct Knob
    {
        int field;
        std::unique_ptr<ni::ui::ParamKnob> control;
    };
    struct Choice
    {
        int field;
        int width;
        std::unique_ptr<ni::ui::ParamSelect> control;
        std::unique_ptr<Caption> caption;
    };

    void layout();
    void refresh();
    int plain (int field) const;

    Model& model;
    ni::ui::EditorFrame window;
    std::unique_ptr<View> view;
    std::unique_ptr<ResponseWell> response;
    std::unique_ptr<ni::ui::Panel> filterPanel;
    std::vector<Knob> knobs;
    std::vector<Choice> choices;
    /* The type's switch (Matched or Adaptive), if it has one, and Bypass. */
    int switchField = -1;
    std::unique_ptr<ni::ui::ParamToggle> typeSwitch, bypassSwitch;

    /* What the controls were last lit for: order, family. */
    std::array<int, 2> shownFor { -1, -1 };

    ni::ui::FixedDesign scaler { window, designWidth, designHeight };
    ni::ui::FrameClock::Subscription frames;

    JUCE_DECLARE_NON_COPYABLE (Editor)
};

} // namespace ni::sf

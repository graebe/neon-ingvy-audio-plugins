// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate's editor: the web editor's App.jsx, natively, on Ultraviolet
 * 1.1.0, laid out as the layout canvas's "NI Trance Gate -- as built"
 * artboard (design/designs/Main.dc.html) and the web editor's app.css.
 *
 * THE WINDOW, inside the window padding (EditorFrame), on the 4px grid:
 *
 *   the top block     468 tall: the ring (240) with the envelope plot (104)
 *                     space-6 under it; space-8 to its right, the three
 *                     compact panels, 390 x 140, space-6 apart -- Gate,
 *                     Envelope, Fade; space-6 to their right, the side
 *                     column, the window verbs in it flush with the panels'
 *                     top edge (the Actions card)
 *   space-6
 *   the settings row  28: Slot, Join Neighbors, Curve, Time in %
 *   space-4
 *   the band          92: the Pattern and Signal plots, the tabs over its
 *                     right edge
 *   space-4
 *   the pads          a row of 40 per sixteen steps, space-2 apart
 *
 * 760 across, which is sixteen pads: the pads decide the content's width and
 * everything lines up with them. The gaps are 1.1.0's -- panels and control
 * groups space-6 apart -- and the proposed artboard's 16 above the plot and
 * under it (canvas TG5 and its window note, +32 over the web editor's 16, 8
 * and 16). The pads start 676 below the window's top edge and THE WINDOW
 * GROWS A ROW AT A TIME with Length -- 784 tall at up to sixteen steps, 1120
 * at 128 -- through FixedDesign, so nothing shrinks and nothing scrolls. The
 * editor asks for its new size itself (setSize); the plugin's window follows
 * its child.
 *
 * WHAT 1.1.0 CHANGED HERE, against the web editor: the window verbs are one
 * joined column of icons in the side column (Verbs.h) where the web had words
 * under the plot and two icons past the window's edge (canvas D1, TG1, TG2);
 * the panels are 390 wide to make room for it, the knobs keeping their cells
 * instead of stretching (TG8); the two-option selects are switches, Fade Dir
 * as "Out" and Env Time as "Time in %" (S7); the Fade panel's switches and
 * buttons stand in two columns at the full 28px (D2), worded in sentence
 * case, verb first -- Set order, Shuffle order (TG7); Rate and Length are
 * stepped knobs, an arrow being one division or one step (Rate has thirteen,
 * more than a Select holds); the ring and the pads are the system's Ring and
 * StepGrid cards; the plots draw in the PlotWell card's data colours
 * (Plots.h); and the hint bar is the kit's, with the Motion switch. What the
 * canvas proposes beyond 1.1.0 -- one plot with a Signal switch (TG3) and a
 * taller envelope plot (TG4) -- is left for the owner.
 *
 * STATE LIVES IN THE MODEL AND THE CONTROLS. tick() reads the model's
 * snapshots and repaints; the editor's one FrameClock calls it on the
 * display's frames. A frame that never comes leaves the picture late, never
 * wrong. What is the editor's own -- Set order and how far it has got, the
 * tab shown, a number being typed -- is what the web editor held too, and
 * none of it is a setting. Message thread only.
 */
#pragma once

#include "Clipboard.h"
#include "FilePanels.h"
#include "Model.h"
#include "Pads.h"
#include "Plots.h"
#include "StepEdits.h"
#include "Verbs.h"

#include "Button.h"
#include "EditorFrame.h"
#include "Fit.h"
#include "Panel.h"
#include "ParamChoiceKnob.h"
#include "ParamControls.h"
#include "Ring.h"
#include "UvTokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace ni::tg
{

class TranceGateEditor final : public juce::Component
{
public:
    static constexpr int designWidth = 824;
    static constexpr int contentWidth = 760;

    /* The top block, from the content's top-left corner. */
    static constexpr int ringSize = 240;
    static constexpr int envelopeY = ringSize + 24;
    static constexpr int envelopeH = 104;
    static constexpr int panelX = ringSize + (int) uv::tok::space::space8;
    static constexpr int panelW = 390;
    static constexpr int panelGap = (int) uv::tok::space::space6;
    static constexpr int topH = 3 * 140 + 2 * panelGap;
    /* The side column, right of the panels. */
    static constexpr int sideX = panelX + panelW + (int) uv::tok::space::space6;
    /* The rows under it. */
    static constexpr int settingsY = topH + (int) uv::tok::space::space6;
    static constexpr int bandY = settingsY + (int) uv::tok::size::controlH + (int) uv::tok::space::space4;
    static constexpr int padsY = bandY + Band::height + (int) uv::tok::space::space4;

    /* The window's height for a pattern of `length` steps. */
    static int designHeightFor (int length);

    /* Everything given must outlive the editor; `clock` is the frame's
     * (EditorFrame). */
    TranceGateEditor (Model&, Clipboard&, FilePanels&, ni::ui::EditorFrame::Clock clock = {});
    ~TranceGateEditor() override;

    /* One frame: the model's snapshots into the picture. The FrameClock's
     * callback, public so a test can run frames on its own clock. */
    void tick (double nowMs);

    /* ---- what the tests reach */
    ni::ui::EditorFrame& frame() noexcept { return window; }
    ni::ui::FixedDesign& fit() noexcept { return scaler; }
    juce::Component& content() noexcept;
    ni::ui::Ring& ring() noexcept { return patternRing; }
    Pads& pads() noexcept { return *padGrid; }
    Band& band() noexcept { return *plotBand; }
    EnvelopePlot& envelopePlot() noexcept { return *envelope; }
    Verbs& verbs() noexcept { return *slotVerbs; }
    StepEdits& stepEdits() noexcept { return edits; }
    ni::ui::Panel& panel (int index) noexcept { return *panels[(std::size_t) index]; }
    ni::ui::Button& orderButton() noexcept { return order; }
    ni::ui::Button& shuffleButton() noexcept { return shuffle; }

    ni::ui::ParamKnob& knob (int parameterIndex);
    ni::ui::ParamSelect& select (int parameterIndex);
    ni::ui::ParamToggle& toggle (int parameterIndex);

    void resized() override;

private:
    class View;

    void layout();
    void layoutPanels();
    void layoutRow();
    /* Set order's button and the conventions, from the edits' state. */
    void refreshOrder();
    /* The pattern's Length changed rows: a new window height. */
    void fitRows (int length);
    void report (const std::string& sentence);
    float plain (int parameterIndex);

    Model& model;
    ni::ui::EditorFrame window;
    std::unique_ptr<View> view;
    StepEdits edits { model };

    ni::ui::Ring patternRing;
    std::unique_ptr<EnvelopePlot> envelope;
    std::unique_ptr<Verbs> slotVerbs;
    std::vector<std::unique_ptr<ni::ui::Panel>> panels;
    std::vector<std::unique_ptr<ni::ui::ParamKnob>> knobs;
    std::unique_ptr<ni::ui::ParamToggle> outSwitch, softSwitch, joinSwitch, timeSwitch;
    std::unique_ptr<ni::ui::ParamSelect> slotSelect, curveSelect;
    ni::ui::Button order { "Set order" }, shuffle { "Shuffle order" };
    std::unique_ptr<Band> plotBand;
    std::unique_ptr<Pads> padGrid;

    /* What the stages' readouts depend on besides their own values. */
    ni::ui::ParamBinding timeParam;

    int rows = 1;
    std::vector<int> detents;
    bool orderShown = false, orderHoles = false;

    ni::ui::FixedDesign scaler;
    ni::ui::FrameClock::Subscription frames;

    JUCE_DECLARE_NON_COPYABLE (TranceGateEditor)
};

} // namespace ni::tg

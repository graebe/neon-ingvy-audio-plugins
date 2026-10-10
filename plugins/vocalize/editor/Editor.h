// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's window, 888 x 532 at its design size, on Ultraviolet: the
 * board "NI Vocalize — proposed" on the NI Plugin Layouts canvas
 * (design/designs/Vocalize-Proposed.dc.html), built from the kit -- the
 * NI Bit-Crusher's grid, so the two read as one family.
 *
 * TWO PICTURES OVER THREE PANELS, on one grid of three 264px columns 16
 * apart:
 *
 *    (32, 32)    VOWEL, one column: the vowel space, F2 across, F1 down
 *    (312, 32)   ENVELOPE, two columns: the sound's colour, in and out
 *    (32, 256)   three standard panels, a column each, 208 tall:
 *
 *                  VOWEL   Vowel, Size, Voice          Capture…, Forget
 *                  VOCAL   Vocal, Resonance, Excite    Mode
 *                  LEVEL   Mix, Output
 *
 *    (0, 488)    the Hint bar: three conventions, Motion, the Signature
 *
 * The vowel chart sits over the panel that moves it. The knobs share one
 * cell width in every panel, so their dials line up across the window.
 * VOWEL is which vowel and whose voice: its buttons capture a voice from a
 * recording and forget it, and a recording dropped anywhere on the window is
 * captured too. VOCAL is how far and how: Mode, a choice of engine, is a
 * select under the knobs it changes the working of. The Vowel knob holds on
 * each of the five vowels as it is dragged past (detents), and travels freely
 * between them.
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

#include "Button.h"
#include "EditorFrame.h"
#include "Fit.h"
#include "Panel.h"
#include "ParamControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace ni::vz
{

class Editor final : public juce::Component,
                     public juce::FileDragAndDropTarget
{
public:
    static constexpr int designWidth = 888;
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

    /* ---- what the tests reach */
    ni::ui::EditorFrame& frame() noexcept { return window; }
    ni::ui::FixedDesign& fit() noexcept { return scaler; }
    juce::Component& content() noexcept;
    VowelChart& vowelChart() noexcept { return *chart; }
    EnvelopePlot& envelopePlot() noexcept { return *envelope; }
    ni::ui::Panel& panel (int index) noexcept { return *panels[(size_t) index]; }
    ni::ui::ParamKnob& knob (int parameterIndex);
    ni::ui::ParamSelect& modeSelect() noexcept { return *mode; }
    ni::ui::Button& captureButton() noexcept { return *captureVoice; }
    ni::ui::Button& forgetButton() noexcept { return *forgetVoice; }

    /* The system's open panel, for a recording to capture from. Replaceable,
     * so a test can answer it without a panel: it is handed the wildcard and
     * calls back with the file chosen, or none. */
    using Chooser = std::function<void (const juce::String& wildcard, std::function<void (const juce::File&)>)>;
    void setChooser (Chooser);

    void resized() override;

    /* ---- a recording dropped on the window */
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    class View;

    void layout();
    /* The buttons as the voice now has them. */
    void showVoice();
    /* The first of `files` that is a recording, if one is. */
    juce::File recordingAmong (const juce::StringArray& files) const;

    Model& model;
    ni::ui::EditorFrame window;
    std::unique_ptr<View> view;
    std::unique_ptr<VowelChart> chart;
    std::unique_ptr<EnvelopePlot> envelope;
    std::array<std::unique_ptr<ni::ui::Panel>, 3> panels;
    std::vector<std::unique_ptr<ni::ui::ParamKnob>> knobs;
    std::unique_ptr<ni::ui::ParamSelect> mode;
    std::unique_ptr<ni::ui::Button> captureVoice, forgetVoice;
    Chooser chooser;
    std::unique_ptr<juce::FileChooser> openPanel;
    std::uint32_t voiceShown = ~0u;

    ni::ui::FixedDesign scaler { window, designWidth, designHeight };
    ni::ui::FrameClock::Subscription frames;

    JUCE_DECLARE_NON_COPYABLE (Editor)
};

} // namespace ni::vz

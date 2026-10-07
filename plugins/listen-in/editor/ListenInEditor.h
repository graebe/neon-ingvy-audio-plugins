// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In's window: which bus, its name, whether it is live, and how loud.
 *
 * Two controls and two displays, and the plugin decides everything: this shows
 * what it decided and hands back the two things a person can change.
 *
 * THE ONE THING WORTH BEING CAREFUL ABOUT is that the bus is a HOST PARAMETER
 * and the name is not. The Bus select is bound to the parameter (ParamSelect),
 * so the host records it, saves it with the set and can automate it, and a
 * change from anywhere -- automation, a preset, another editor -- arrives the
 * way the select's own does. The name is the model's (Model::setLabel): text,
 * which the plugin keeps in its own state. A name is not a number.
 *
 * THE LAYOUT, the canvas's "NI Listen-In -- proposed" artboard as far as
 * Ultraviolet 1.1.0 decides it, 360 x 172, on the 4px grid:
 *
 *   32   space-8, the window padding
 *   28   Bus (label 28, select 64), space-4, the name field to the edge
 *   16   space-4
 *   28   the status LED in a cell as wide as its longest word, space-4, the
 *        level meter (12, centred on the row) to the edge
 *   24   space-6
 *   28   the hint bar: the conventions, Motion, the Signature
 *   16   space-4
 *  ----
 *  172   and across, 32 + 296 + 32 = 360
 *
 * No title row: the host names the window. No panels: the Ground's rings
 * reflect off the window border only (EditorFrame).
 *
 * WHAT DIFFERS FROM THE WEB EDITOR, each because 1.1.0 says so:
 *   - the status is an LED with the status word as its label, not a word on a
 *     row of its own (Toggle card: "a state the plugin reports is an LED"),
 *     which takes the row away: 232 tall before, 172 now;
 *   - the bar states the conventions at rest (Hint card); the bus and its
 *     name, and why a bus is not live, are the LED's line (InfoLines.h);
 *   - the field and the meter are the 1.1.0 cards (TextField, Meter), which
 *     were drawn from this editor's.
 *
 * STATE LIVES IN THE MODEL. refresh() reads it -- the status, the peak, the
 * name -- and shows it; the frame clock calls it once a frame while the window
 * is open, and anything else may call it too. A missed frame shows the truth a
 * frame late, never something else.
 *
 * A FIXED DESIGN SIZE, SCALED. Everything is laid out at 360 x 172 inside a
 * FixedDesign, which scales it to whatever size the editor is given;
 * constrain() keeps the host's window in proportion. Message thread only.
 */
#pragma once

#include "EditorFrame.h"
#include "Fit.h"
#include "Led.h"
#include "Luminous.h"
#include "Meter.h"
#include "Model.h"
#include "ParamControls.h"
#include "TextField.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::li
{

/* The meter's floor: its well is linear in dB from here to 0 dBFS. */
inline constexpr float meterFloorDb = -60.0f;

/*
 * A peak, linear 0..1, as a share of the meter's well -- ui/src/lib/state.js's
 * meterFraction. The well is linear in dB, not in amplitude: a linear bar
 * spends nine tenths of its length on the top 20 dB and reads as empty for a
 * quiet part that is plainly audible, which would make this window say "not
 * working" about a bus that is.
 */
float meterFraction (float peak);

/* The LED a status lights: on while live, amber while another holds the bus,
 * red when there is no bus to be had, dark while starting. */
ni::ui::Led::Status ledStatus (Status);

class ListenInEditor final : public juce::Component
{
public:
    static constexpr int designWidth = 360;
    static constexpr int designHeight = 172;

    /* `model` must outlive the editor. `clock` is the frame's (EditorFrame),
     * injected by a test. */
    explicit ListenInEditor (Model& model, ni::ui::EditorFrame::Clock clock = {});
    ~ListenInEditor() override;

    /* Reads the model and shows it. */
    void refresh();

    /* The host window's resize rules: the design's aspect, half to twice its
     * size. */
    static void constrain (juce::ComponentBoundsConstrainer&);

    /* The parts, for the tests and the processor's editor. */
    ni::ui::EditorFrame& frame() noexcept { return window; }
    ni::ui::FixedDesign& design() noexcept { return fit; }
    ni::ui::ParamSelect& busSelect() noexcept { return content.bus; }
    ni::ui::TextField& nameField() noexcept { return content.name; }
    ni::ui::Led& statusLed() noexcept { return content.status; }
    ni::ui::Meter& levelMeter() noexcept { return content.level; }

    void resized() override;

private:
    /* What sits inside the window's padding. Its controls' light -- the LED's
     * halo, the meter's glow, a focus ring -- reaches into the padding, so it
     * is Luminous and passes that light up to the frame (ChildLights.h). */
    struct Content final : public juce::Component, public ni::ui::Luminous
    {
        explicit Content (Model&);

        void paint (juce::Graphics&) override;
        void paintLight (juce::Graphics&) override;
        void resized() override;

        ni::ui::ParamSelect bus;
        ni::ui::TextField name;
        ni::ui::Led status;
        ni::ui::Meter level;
        int statusWidth = 0;
    };

    void nameTyped (const juce::String&);

    Model& model;
    ni::ui::EditorFrame window;
    Content content;
    ni::ui::FixedDesign fit;
    ni::ui::FrameClock::Subscription frames;

    JUCE_DECLARE_NON_COPYABLE (ListenInEditor)
};

} // namespace ni::li

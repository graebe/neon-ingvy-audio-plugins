// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE PLOT: one well, the audio behind, the shape in front -- the web
 * editor's Shaper.jsx, natively.
 *
 * ONE ALIGNMENT IS LOAD-BEARING: the shape you drag sits directly above the
 * audio it shaped, on one axis spanning exactly one cycle. The capture is
 * phase-locked to the trigger and spans one cycle, so anything else would put
 * the drawn dip somewhere other than the measured one. A shape longer than a
 * cycle overruns the right edge and is MARKED, in amber, rather than
 * accommodated.
 *
 * FOUR LAYERS, BACK TO FRONT, AND EACH ANSWERS A DIFFERENT QUESTION:
 *
 *   the input      plot-dry at half opacity: what arrived. Context, not the
 *                  subject, so it shows as a halo around the output wherever
 *                  the duck took something away.
 *   the output     uv with the arc glow: the audio that left, and the
 *                  picture's mass.
 *   the envelope   the gain actually applied, measured, as a 1px plot-ghost
 *                  line mirrored about the centre -- the ceiling the output
 *                  could reach. There when the track is silent, and invisible
 *                  until it parts from the shape, which is when it matters: a
 *                  retrigger part way through a recovery anchors on the level
 *                  the envelope had reached, and no drawing can predict that.
 *   the shape      what you ASKED for: an ink line on a bg-000 casing, with
 *                  four handles. Ink, not uv, because ink over uv is only
 *                  1.34:1 -- the two differ by kind and hue, not brightness.
 *
 * Over them the playhead (the capture's sweep, a line-200 rule: the PlotWell
 * card's position mark), the overrun mark and the millisecond ruler, with the
 * cycle's length and the instant the duck reaches its floor as the ruler's
 * landmark.
 *
 * FOUR HANDLES, AND EACH IS A HOST PARAMETER (docs/live.md, "The shape well"):
 *
 *   start     sideways   Delay      "when it begins"
 *   bottom    sideways   Attack     "how fast"
 *             up/down    Depth      "how far"
 *   holdEnd   sideways   Hold
 *   end       sideways   Release
 *
 * A drag is one gesture on its parameter -- on both, for the bottom corner,
 * opened and closed together so undo is one step -- and lands in the host's
 * undo history and automation lane like a knob. The handles do not animate: a
 * curve editor whose handles glide is one you cannot aim at.
 *
 * THE PICTURE IS THE MODEL'S: the curve and the handles' places are the
 * engine's (Model::shapeGain, shapeMarks), the audio and the envelope its
 * capture. update() reads them, on the editor's frame tick; nothing here is
 * state of its own but the paths it keeps between frames.
 */
#pragma once

#include "Model.h"

#include "Focus.h"
#include "ParamBinding.h"
#include "Plot.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace ni::sc
{

class Shaper final : public ni::ui::PlotWell
{
public:
    /* The well's geometry, the web editor's: the ruler's band under the
     * content, the drag's dead zone and Shift's ratio (the Knob's). */
    static constexpr float axisH = 14.0f;
    static constexpr float deadZone = 4.0f;
    static constexpr float fineRatio = 5.0f;
    static constexpr float handleR = 4.5f;

    /* The handles, in their order along the cycle. */
    enum class Which { start, bottom, holdEnd, end };

    class Handle;

    explicit Shaper (Model&);
    ~Shaper() override;

    /* Reads the model again -- the shape, its marks, the capture, the sweep --
     * and repaints. The editor's frame tick. */
    void update();

    /* The audio area: bipolar, centred, under the caption, over the ruler. */
    float top() const;
    float bottom() const;
    float mid() const;
    float half() const;
    float plotWidth() const;

    /* The x of a position in the cycle, 0..100 %, clamped; the y of a gain on
     * the upper edge of the mirrored envelope. */
    float xOf (double percent) const;
    float yOf (double gain) const;

    Handle& handle (Which);
    /* Where a handle's dot is, in the well's pixels. */
    juce::Point<float> handlePoint (Which) const;

    /*
     * HOW A STAGE READS AND IS TYPED on its handle, by parameter index: the
     * editor's, the same as the stage knobs', so a screen reader hears "175
     * ms" on the Release handle when the Release knob says it, and typing
     * "40" means the same on both. Unset, a handle reads and takes its
     * parameter's own text.
     */
    using StageText = std::function<juce::String (int index)>;
    using StageParse = std::function<std::optional<float> (int index, const juce::String& typed)>;
    void setStageText (StageText, StageParse);

    /* What the well shows now, for the tests. */
    bool overruns() const noexcept { return marks.span > 100.0 + 1.0e-9; }
    const ShapeMarks& getMarks() const noexcept { return marks; }
    double playhead() const noexcept { return sweep; }
    /* Whether nothing is reaching the plugin (Status.h's hasInput). */
    bool isQuiet() const noexcept { return quiet; }
    const ni::ui::plot::Axis& axis() const noexcept { return ruler; }

    void resized() override;

private:
    friend class Handle;

    void paintPlot (juce::Graphics&) override;
    void layoutHandles();
    void rebuildShape();
    void rebuildEnvelope();
    void refreshCaption();

    ni::ui::ParamBinding& binding (int index);

    Model& model;

    /* One binding per parameter a handle moves, shared by the handles: every
     * change, from anywhere, re-reads the shape. */
    std::array<std::unique_ptr<ni::ui::ParamBinding>, param::count> bindings;
    std::array<std::unique_ptr<Handle>, 4> handles;

    ShapeMarks marks;
    double sweep = 0.0;
    double spanMs = 0.0;
    double stagesMs = 0.0;
    bool quiet = false;

    StageText textOfStage;
    StageParse parseForStage;

    /* Kept from frame to frame, so a frame allocates nothing once warm. */
    std::vector<float> gains, lowest;
    juce::Path intended, envelope, dryBand, wetBand;
    ni::ui::plot::Axis ruler;

    JUCE_DECLARE_NON_COPYABLE (Shaper)
};

/*
 * ONE HANDLE: a small ink dot in a generous target -- a handle you cannot grab
 * reads as one that does not work -- that drags, steps and resets the
 * parameters it stands for. Under the pointer or with visible keyboard focus
 * it lights uv; keyboard focus adds glow-focus. A slider to assistive
 * technology, named for what it moves.
 */
class Shaper::Handle final : public juce::Component
{
public:
    static constexpr int size = 32;
    static constexpr float targetR = Shaper::handleR * 3.0f;

    Handle (Shaper&, Which, int xParam, int yParam);

    Which which() const noexcept { return kind; }
    int xParameter() const noexcept { return xIndex; }
    int yParameter() const noexcept { return yIndex; }

    /* What the readers' "value" is: the stage's text as its knob reads it,
     * then Depth's on the bottom corner, joined. */
    juce::String valueText() const;

    /* Typed text, read as the stage's knob reads it: one committed edit, or
     * nothing for text it cannot read. */
    void typeValue (const juce::String&);

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics&) override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    bool isLit() const noexcept { return hovered || focus.isVisible(); }

private:
    struct Drag
    {
        juce::Point<float> from;   // the press, in the well's pixels
        float plainX = 0.0f;       // the x parameter's value at the press
        float plainY = 0.0f;       // and the y's
        bool fine = false;         // Shift at the press, never re-read
        bool moved = false;        // out of the dead zone yet
    };

    void commitKey (int index, float delta, std::optional<float> to);

    Shaper& shaper;
    const Which kind;
    const int xIndex, yIndex;   // yIndex -1 for none
    bool hovered = false;
    std::optional<Drag> drag;
    ni::ui::FocusVisibility focus { *this };
};

} // namespace ni::sc

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The window every editor is drawn in -- the web kit's EditorFrame.jsx, and
 * what Ultraviolet 1.1.0 gives every plugin window, so that no editor lays it
 * out itself.
 *
 * THE WINDOW, FROM THE BACK:
 *
 *   the Ground       the animated ground, the window's size, first child: it
 *                    plays the model's rings (EditorModel::takeRings) and
 *                    breaks them around every wave source in the window
 *   the content      the editor's own component, laid out from the top --
 *                    "nothing is centred vertically" -- inside the window
 *                    padding: space-8 at the top and sides
 *   the Hint bar     pinned to the bottom edge, space-4 above it, its rule the
 *                    full width of the window and its text inside the
 *                    padding; space-6 between it and the content. It carries
 *                    the Motion switch and the Signature
 *
 * There is no title row: the window does not repeat the plugin's name, the
 * host shows it (as the web editors have it).
 *
 * THE WINDOW'S OWN STATE, WIRED ONCE. The Motion switch shows what the model
 * holds (EditorModel::motion) and asks it to change; the Ground follows it.
 * The switch's line is the kit's (motionInfo), so it reads the same in every
 * window. The hint's info comes from an InfoTracker on the whole window, so a
 * control anywhere in the content needs nothing but its line (setInfo). An
 * action's
 * outcome stays for outcomeMs, timed by the clock: what the bar shows is a
 * function of the time now, and the timer only refreshes it.
 *
 * A PRESS ELSEWHERE ENDS A TYPED NAME, as a click anywhere on the web page
 * blurred its input. JUCE moves the keyboard only to a component that takes
 * it, and a press on the window's background, an LED, a meter or the hint's
 * text lands on one that does not, so the field would keep the keyboard and
 * its edit open. The frame hears every press in the window (pressed()) and
 * ends the edit of a TextField the press was not on: the field keeps what
 * was typed and lets the keyboard go. (A Readout's field ends its own: it is
 * modal while open.)
 *
 * LIGHT. The Ground is opaque, so the light of what stands on it cannot be
 * painted by this component (its paint() is under every child). It is painted
 * by the window layer above the Ground, which holds the content and the bar
 * and ends its paint() with paintChildLights(): the signature's halo over the
 * rule, the Motion switch's focus ring, and the light of the content if the
 * content is Luminous -- which it has to be when a control of its own can
 * glow past the content's edge into the padding (ChildLights.h).
 *
 * A FIXED DESIGN SIZE. The frame is laid out at the editor's design size and
 * scaled by FixedDesign (Fit.h); heightFor() is the arithmetic for an editor
 * whose height follows its content (the Trance Gate's rows of steps).
 *
 * It also owns the editor's one FrameClock, for whatever in the content
 * animates. Message thread.
 */
#pragma once

#include "EditorModel.h"
#include "FrameClock.h"
#include "Ground.h"
#include "Hint.h"
#include "Info.h"
#include "Toggle.h"
#include "UvTokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

namespace ni::ui
{

class EditorFrame final : public juce::Component,
                          public InfoHost,
                          private juce::Timer
{
public:
    /* Milliseconds, monotonic; injected by a test, the real one otherwise. */
    using Clock = std::function<double()>;

    /* The window padding: space-8 at the top and either side, space-4 under
     * the bar, space-6 between the content and the bar. */
    static constexpr int padding = (int) uv::tok::space::space8;
    static constexpr int bottomPadding = (int) uv::tok::space::space4;
    static constexpr int barGap = (int) uv::tok::space::space6;

    /* How long an action's outcome stays in the bar: the web editors' 6 s. */
    static constexpr double outcomeMs = 6000.0;

    /* The window height that gives the content `contentHeight`. */
    static constexpr int heightFor (int contentHeight)
    {
        return padding + contentHeight + barGap + Hint::height + bottomPadding;
    }

    /* `model` must outlive the frame. */
    explicit EditorFrame (EditorModel& model, Clock clock = {});
    ~EditorFrame() override;

    /* The editor's component, placed at contentBounds(), above the Ground and
     * beside the bar. Not owned; nullptr for none. */
    void setContent (juce::Component*);
    juce::Rectangle<int> contentBounds() const;

    /* The window's conventions: (verb, rest), at most three. */
    void setConventions (std::vector<Clause>);
    /* An action's outcome, in the first clause's place for outcomeMs from now;
     * a second one replaces the first and starts the time again. */
    void showOutcome (Clause);
    void clearOutcome();
    /* The outcome shown at the clock's time now, if any. */
    std::optional<Clause> outcome() const;

    /* The Motion switch's line in every window, which the frame gives it. */
    static constexpr InfoText motionInfo {
        "Motion — ripple the background on the beat; remembered on this computer."
    };

    /* The bar's own two lines: the Motion switch's (motionInfo unless an
     * editor says otherwise) and the Signature's. */
    void setMotionInfo (const juce::String&);
    void setSignatureInfo (const juce::String&);

    /* The model's Motion changed elsewhere (another editor of the same
     * plugin): show it, and start or stop the Ground. Called by the frame
     * itself after it asks the model. */
    void motionChanged();

    /* Ends an outcome or an info grace that has run out by the clock: what
     * the timers do, and what a test does after moving its clock. */
    void poll();

    /* A press landed on `at`, after `at` had it: a name being typed anywhere
     * else in the window is kept. What every press in the window calls, and
     * what a test calls for one. */
    void pressed (juce::Component& at);

    Ground& ground() noexcept { return groundLayer; }
    Hint& hint() noexcept { return bar; }
    Toggle& motionSwitch() noexcept { return motionToggle; }
    FrameClock& frameClock() noexcept { return frames; }
    InfoState& infoState() override { return info; }

    void resized() override;

private:
    /* Above the Ground, the window's size: the content, the bar, and their
     * light over the Ground. */
    class Layer final : public juce::Component
    {
    public:
        Layer();
        void paint (juce::Graphics&) override;
    };

    /* Every press in the window, to pressed(). */
    class PressListener final : public juce::MouseListener
    {
    public:
        explicit PressListener (EditorFrame&);
        ~PressListener() override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        EditorFrame& frame;
    };

    void timerCallback() override { poll(); }
    void refreshBar();

    EditorModel& model;
    Clock clock;

    Ground groundLayer;
    Layer layer;
    Hint bar;
    Toggle motionToggle { "Motion" };
    juce::Component::SafePointer<juce::Component> content;

    InfoState info;
    InfoTracker tracker { *this, info };
    PressListener presses { *this };
    FrameClock frames;

    std::optional<Clause> shownOutcome;
    double outcomeAt = 0.0;

    JUCE_DECLARE_NON_COPYABLE (EditorFrame)
};

} // namespace ni::ui

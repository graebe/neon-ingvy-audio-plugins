// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The window every editor is drawn in -- the web kit's EditorFrame.jsx, and
 * what Ultraviolet gives every plugin window, so that no editor lays it
 * out itself.
 *
 * THE WINDOW, FROM THE BACK:
 *
 *   the ground       bg-000 under the dot paper (UvGround.h), the frame's own
 *                    paint: still, the window's size, under every child
 *   the content      the editor's own component, laid out from the top --
 *                    "nothing is centred vertically" -- inside the window
 *                    padding: space-8 at the top and sides
 *   the Hint bar     pinned to the bottom edge, space-4 above it, its rule the
 *                    full width of the window and its text inside the
 *                    padding; space-6 between it and the content. It carries
 *                    the Signature
 *
 * There is no title row: the window does not repeat the plugin's name, the
 * host shows it (as the web editors have it).
 *
 * THE WINDOW'S OWN STATE, WIRED ONCE. The Signature's line is the kit's
 * (signatureInfo), so it reads the same in every window. The hint's info comes
 * from an InfoTracker on the whole window, so a control anywhere in the
 * content needs nothing but its line (setInfo). An action's outcome stays for
 * outcomeMs, timed by the clock: what the bar shows is a function of the time
 * now, and the timer only refreshes it.
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
 * LIGHT. The frame's paint() is the ground, under every child, so the light
 * of what stands on the ground cannot be painted there. It is
 * painted by the window layer over the ground, which holds the content and
 * the bar and ends its paint() with paintChildLights(): the signature's halo
 * over the rule, and the light of the content if the content is Luminous --
 * which it has to be when a control of its own can glow past the content's
 * edge into the padding (ChildLights.h).
 *
 * A FIXED DESIGN SIZE. The frame is laid out at the editor's design size and
 * scaled by FixedDesign (Fit.h); heightFor() is the arithmetic for an editor
 * whose height follows its content (the Trance Gate's rows of steps).
 *
 * It also owns the editor's one FrameClock, for whatever in the content
 * animates. Message thread.
 */
#pragma once

#include "FrameClock.h"
#include "Hint.h"
#include "Info.h"
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

    explicit EditorFrame (Clock clock = {});
    ~EditorFrame() override;

    /* The editor's component, placed at contentBounds(), above the ground and
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

    /* The Signature's line in every window, which the frame gives it: the
     * Hint card's one voice. */
    static constexpr InfoText signatureInfo { "Neon Ingvy — the publisher of this plugin." };

    /* The Signature's line, for an editor that has a reason to say it
     * otherwise (signatureInfo until then). */
    void setSignatureInfo (const juce::String&);

    /* Ends an outcome or an info grace that has run out by the clock: what
     * the timers do, and what a test does after moving its clock. */
    void poll();

    /* A press landed on `at`, after `at` had it: a name being typed anywhere
     * else in the window is kept. What every press in the window calls, and
     * what a test calls for one. */
    void pressed (juce::Component& at);

    Hint& hint() noexcept { return bar; }
    FrameClock& frameClock() noexcept { return frames; }
    InfoState& infoState() override { return info; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /* Over the ground, the window's size: the content, the bar, and their
     * light over the ground. */
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

    Clock clock;

    Layer layer;
    Hint bar;
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

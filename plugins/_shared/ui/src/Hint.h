// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The hint bar at the bottom edge of every window -- the Hint card of
 * Ultraviolet 1.1.0, and the web kit's Hint.jsx.
 *
 * ONE LINE OF `hint` TEXT UNDER A line-100 RULE. At rest it states the
 * window's conventions: each clause's verb in ink and the rest in ink-muted,
 * clauses separated by en dashes in ink-dim with 12px either side. Three
 * clauses at most -- "if a window needs more, the interaction is too clever".
 * The tips are left-aligned and truncate with an ellipsis rather than shrink.
 *
 * WHAT IT SHOWS, IN ORDER (Info.h has the rules and hintClauses the
 * arithmetic): an action's outcome in the first clause's place while there is
 * one; else the info line of the control under the pointer or on the visible
 * keyboard focus, laid OVER the conventions -- name in ink, the dash and the
 * rest in ink-muted -- with the conventions still sizing the tips underneath;
 * else the conventions.
 *
 * THEN THE WINDOW'S OWN TWO, WHICH NEVER MOVE. In a window with a Ground the
 * Motion switch follows the tips, space-4 after them; the Signature closes the
 * bar on the right. 1.1.0's guarantee: Motion sits at least 16px (space-4)
 * before the Signature, and the tips give way before that gap does. The tips
 * are as wide as the CONVENTIONS -- not as the line or the outcome laid over
 * them, which truncate in that width -- so nothing a pointer or an action
 * does can move the switch.
 *
 * The bar owns the Signature (exactly once per window, at the right end, so an
 * editor cannot forget it, move it or show two) and places the Motion switch
 * it is handed (the EditorFrame's, which owns the state). The bar is
 * transparent: the window ground, and the Ground's rings, show under it.
 *
 * Its children's light reaches past it -- the signature's halo over the rule,
 * the switch's focus ring -- so the bar is Luminous and passes that light on
 * to the window (ChildLights.h).
 */
#pragma once

#include "Info.h"
#include "Luminous.h"
#include "Signature.h"
#include "UvTokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

namespace ni::ui
{

class Hint final : public juce::Component, public Luminous
{
public:
    Hint();
    ~Hint() override;

    /* The bar: its 1px rule and 27px under it -- control-h. */
    static constexpr int height = (int) uv::tok::size::controlH;
    /* The text's line box, under the rule, centred in what is left. */
    static constexpr int lineTop = 8;
    /* Tips to Motion, and the least there is between Motion and the
     * Signature: space-4. */
    static constexpr int gap = (int) uv::tok::space::space4;
    /* .sep { margin: 0 12px } */
    static constexpr float separatorMargin = 12.0f;
    static constexpr int maxClauses = 3;

    /* The window's conventions: (verb, rest), at most three. */
    void setConventions (std::vector<Clause>);
    const std::vector<Clause>& getConventions() const noexcept { return conventions; }

    /* The info line on show, or empty: what InfoState::text() says. */
    void setInfoLine (const juce::String&);
    /* An action's outcome while it is shown, or none. */
    void setOutcome (std::optional<Clause>);

    /* What is drawn now, by the precedence above. */
    HintContent content() const;

    /* The window's padding either side: the bar runs the full width of the
     * window, its text inside the window's own padding. */
    void setPadding (int horizontal);

    /* The Motion switch to place after the tips, or nullptr for a window
     * without a Ground. Not owned. */
    void setMotionSwitch (juce::Component*);

    Signature& signature() noexcept { return sig; }

    /* Where things are: the tips' cell, the switch, the signature. */
    juce::Rectangle<int> tipsBounds() const;
    juce::Rectangle<int> motionBounds() const;

    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;
    void resized() override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void changed();

    std::vector<Clause> conventions;
    juce::String infoLine;
    std::optional<Clause> outcome;

    int padding = (int) uv::tok::space::space8;
    juce::Component* motion = nullptr;
    Signature sig;
    juce::Rectangle<int> tips;

    JUCE_DECLARE_NON_COPYABLE (Hint)
};

/* The width `clauses` take in the bar, separators included: what the tips
 * would be without a limit. */
float clausesWidth (const std::vector<Clause>&);

} // namespace ni::ui

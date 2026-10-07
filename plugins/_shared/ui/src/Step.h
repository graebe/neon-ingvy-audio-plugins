// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * One cell of a sequencer: the Step card of Ultraviolet 1.1.0, and the
 * Trance Gate web editor's .pad.
 *
 * A 40px SQUARE WELL, bg-200 on a line-100 hairline. What it shows comes in
 * two halves, and the rule that joins them is the card's and the web pads':
 * THE BORDER IS WHAT WAS DRAWN, THE FILL IS WHAT IS HEARD.
 *
 *   drawn      off, on or tie -- what the user set
 *   amount     the drawn step's amount, 0..1
 *   level      how much of the step sounds NOW, as the plugin's own level
 *              factor: 1 for a step that sounds as drawn and 0 for a gap,
 *              whatever it was drawn as. A plain sequencer gives 1 for a step
 *              that is on and 0 for one that is off (levelAsDrawn); a fade
 *              moves it in between
 *
 * and from the two, every state the card and the fade need:
 *
 *   on         a uv border and glow-led, lit uv from the bottom to amount x
 *              level -- never under 5 %, so an on step always shows; at full
 *              amount the card's solid fill, below it the card's "partial"
 *   tie        a hollow uv outline, the hairline doubled inward to 2px, with a
 *              2px uv bar across its middle; no glow, as it does not trigger
 *   pending    drawn on or tied but not sounding (level 0): the uv border with
 *              no fill, no bar and no glow -- in the pattern, not playing yet
 *   filled     drawn off but sounding (a hole a fade has not removed yet):
 *              the fill at 0.55 and no uv border, so it is never mistaken for
 *              a step that was drawn
 *   gap        neither: the bare well
 *
 * and over those, what the window says about the step:
 *
 *   play       the playhead: a bg-300 well, a uv border and glow-led; on a lit
 *              step the fill is shaded with --dip (bg-000 at a quarter), on
 *              any other a uv-glow wash at half opacity, as the card's
 *              .ph-step.play:not(.on)::before
 *   beat       the first step of a beat: a line-200 border where nothing else
 *              colours it, so bars read without numbers
 *   accent     an amber border
 *   cursor     the step being edited: a 1px ink outline 2px outside the cell
 *   waiting    a bg-100 well: a step a mode is still waiting on (the Trance
 *              Gate's Set order)
 *   number     10px hint text at the top left; 0 for none. Each part of it
 *              takes the colour that reads on what is behind that part: on-uv
 *              over a full uv fill, bg-000 over a hole lit at filledAlpha
 *              (5.7:1), and over the well ink-dim for the card's index, which
 *              is a mark -- or ink where the number is a control you press
 *              (numberIsControl: the Trance Gate's arrival numbers), since
 *              1.1.0 keeps ink-dim for disabled text and inactive marks only.
 *              A fill that stops part way up the number splits it, rather
 *              than leaving one part on a colour it cannot be read on
 *
 * Everything outside the cell -- glow-led's halo, the cursor's outline -- is
 * light past its bounds, painted by the parent (Luminous.h): a StepGrid.
 *
 * IT TAKES NO INPUT ITSELF. A Step is drawn and named; what a press, a drag
 * or a key does is the grid's (StepGrid.h), so one gesture owns a press that
 * runs across several steps. A component may sit inside it (the Trance Gate's
 * arrival number), over the fill.
 */
#pragma once

#include "Luminous.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui
{

class Step : public juce::Component,
             public Luminous
{
public:
    /* The cell's side: size::step. */
    static constexpr int size = 40;
    /* The least lit height of a step that sounds: an on step always shows. */
    static constexpr float minLit = 0.05f;
    /* A hole that still sounds is lit at this alpha. */
    static constexpr float filledAlpha = 0.55f;

    enum class Drawn
    {
        off,
        on,
        tie,
    };

    struct State
    {
        Drawn drawn = Drawn::off;
        float amount = 1.0f;
        float level = 0.0f;
        bool play = false;
        bool beat = false;
        bool accent = false;
        bool cursor = false;
        bool waiting = false;
        int number = 0;
        bool numberIsControl = false;

        bool operator== (const State& o) const
        {
            return drawn == o.drawn && juce::exactlyEqual (amount, o.amount)
                && juce::exactlyEqual (level, o.level) && play == o.play && beat == o.beat
                && accent == o.accent && cursor == o.cursor && waiting == o.waiting
                && number == o.number && numberIsControl == o.numberIsControl;
        }
        bool operator!= (const State& o) const { return ! (*this == o); }
    };

    /* The level of a step with no fade over it: 1 when drawn, 0 when not. */
    static constexpr float levelAsDrawn (Drawn d) noexcept { return d == Drawn::off ? 0.0f : 1.0f; }

    Step();
    ~Step() override;

    void setState (const State&);
    const State& getState() const noexcept { return state; }

    /* What the state comes to. */
    bool isPending() const noexcept;
    bool isFilled() const noexcept;
    /* Whether it is lit: a fill that sounds, on a step drawn on or off. */
    bool isLit() const noexcept;
    /* The lit height, 0..1, or 0 for none. */
    float litHeight() const noexcept;
    /* The top of the fill in the cell's coordinates: its height when there
     * is none. */
    float fillTop() const noexcept;
    /* The colour the number takes at height `y` in the cell. */
    juce::Colour numberColourAt (float y) const noexcept;

    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;

private:
    bool glows() const noexcept;

    State state;

    JUCE_DECLARE_NON_COPYABLE (Step)
};

} // namespace ni::ui

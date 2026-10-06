// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The circle of fifths: twelve keys on a ring, C at the top and a fifth
 * further at each step clockwise, and the one the user chose.
 *
 *   a key           a 36px disc, bg-100 on a line-100 hairline, its name in
 *                   ink-dim, set in the button style
 *   in the scale    bg-200 on line-200, the name ink-muted: the seven notes of
 *                   the chosen key and mode, which on this circle are always
 *                   seven neighbours
 *   sounding        the hairline and the name lit uv, with glow-led -- light
 *                   past the ring's own bounds is painted by its parent
 *                   (Luminous.h), so a container ends its paint() with
 *                   ni::ui::paintChildLights
 *   the root        filled uv, the name on-uv, with glow-led
 *   dimmed          uv-deep instead of uv, and no glow: kept, not sounding
 *   the tonic       a 1px amber ring 4px outside its disc: the window's one
 *                   amber mark, which is why nothing else here is amber
 *   hover           the disc under the pointer rises to bg-300
 *   focus           glow-focus round the tonic, for the keyboard only
 *
 * The centre is the owner's: centre() is the box inside the ring, where an
 * editor puts the mode. The circle sets its caption -- the key signature --
 * along the bottom of that box, in the hint style, ink-dim.
 *
 * IT DOES NOT CHOOSE BY ITSELF. A click on a key, or an arrow (Right and Up a
 * fifth clockwise, Left and Down back, Home to C), asks for that tonic with
 * onTonicSelected; the owner sets the state from what the model then holds.
 * One Tab stop. To an accessibility client it is an adjustable value, the
 * tonic, stepped a fifth at a time.
 */
#pragma once

#include "Focus.h"
#include "Luminous.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <functional>

namespace ni::ui
{

class CircleOfFifths : public juce::Component,
                       public Luminous
{
public:
    struct State
    {
        /* The chosen tonic, as a pitch class. */
        int tonic = 0;
        /* Pitch classes, C at bit 0: the key's seven, and those sounding. */
        std::uint16_t scale = 0;
        std::uint16_t lit = 0;
        /* The sounding chord's root, or -1. */
        int root = -1;
        bool dimmed = false;
        /* Each key's name, by pitch class. */
        std::array<juce::String, 12> names { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        /* Under the centre: the key signature. */
        juce::String caption;

        bool operator== (const State& o) const
        {
            return tonic == o.tonic && scale == o.scale && lit == o.lit && root == o.root && dimmed == o.dimmed
                && names == o.names && caption == o.caption;
        }
        bool operator!= (const State& o) const { return ! (*this == o); }
    };

    CircleOfFifths();
    ~CircleOfFifths() override;

    void setState (const State&);
    const State& getState() const noexcept { return state; }

    /* Asked for, with the tonic's pitch class. */
    std::function<void (int)> onTonicSelected;

    /* The pitch class at a place on the ring (0 at the top, clockwise), and
     * the place of a pitch class. */
    static constexpr int pitchAt (int place) noexcept { return ((place % 12 + 12) % 12) * 7 % 12; }
    static constexpr int placeOf (int pitchClass) noexcept { return ((pitchClass % 12 + 12) % 12) * 7 % 12; }

    /* Where a key's disc is, the free box inside the ring (wider than tall:
     * the owner's mode select fits across it), and the key under a point
     * (-1 for none). */
    juce::Rectangle<float> disc (int pitchClass) const;
    juce::Rectangle<float> centre() const;
    int keyAt (juce::Point<float>) const;

    static constexpr float discSize = 36.0f;

    void paint (juce::Graphics&) override;
    /* The lit discs' glow past the ring's bounds (Luminous.h). */
    void paintLight (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    float radius() const;
    /* Whether a disc glows: sounding or the root, and not dimmed. */
    bool glows (int pitchClass) const;
    void ask (int pitchClass);

    State state;
    int hovered = -1;
    FocusVisibility focus { *this };

    JUCE_DECLARE_NON_COPYABLE (CircleOfFifths)
};

} // namespace ni::ui

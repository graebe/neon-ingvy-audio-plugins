// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * How a piano key is drawn, wherever one is: the Keyboard across a window and
 * the PianoAxis up the side of a pitch view paint their keys here, so both
 * take the same colours from the same tokens and a change to one is a change
 * to both.
 *
 *   white key   bg-300; lit, the lit colour
 *   black key   bg-000 on a line-200 hairline; lit, the lit colour, its
 *               hairline too
 *   lit         uv; uv-deep when the owner dims it -- what was held and is
 *               being kept, not what sounds
 *   a C's name  the hint style, ink-dim; on a lit key on-uv
 *
 * WHERE A KEY IS, how long and which way it points, is the caller's: these
 * fill the rectangle they are given. The proportions both share are here.
 */
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace ni::ui::keypaint
{

/* A black key's share of a white key's length, and of its width. */
inline constexpr float blackShare = 0.6f;
/* The gap between two white keys, px. */
inline constexpr float gap = 1.0f;

/* The colour a lit key takes: uv, or uv-deep dimmed. */
juce::Colour litColour (bool dimmed);

/* A white key filling `key`, lit in `lit` when `on`. */
void whiteKey (juce::Graphics&, juce::Rectangle<float> key, bool on, juce::Colour lit);

/* A black key filling `key`, lit in `lit` when `on`. */
void blackKey (juce::Graphics&, juce::Rectangle<float> key, bool on, juce::Colour lit);

/* A key's name ("C3") in the hint style, placed in `box` by `just`: ink-dim,
 * or on-uv on a lit key. */
void keyName (juce::Graphics&, const juce::String& name, juce::Rectangle<float> box, bool on,
              juce::Justification just = juce::Justification::centred);

} // namespace ni::ui::keypaint

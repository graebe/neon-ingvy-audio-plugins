// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The window ground at rest: bg-000 under dot paper and a trace of violet
 * grain, exactly the design's `.ph-window` background.
 *
 * From the system (README, Colour): `bg-dot` 1px dots at a 12px pitch, and over
 * them a 5 % `uv-deep` noise, uniform white noise and not fractal, as a 96px
 * tile. In bundle.css that is two layers on bg-000 --
 *
 *   radial-gradient(circle, bg-dot 1px, transparent 1.2px), 12px tiles at 6px 6px
 *   the grain PNG, 96px tiles at 0 0, on top
 *
 * -- so the dots sit on the multiples of 12 and the whole picture repeats
 * every 96px. tile() is that picture, built once from the tokens and the
 * design's own grain PNG (scripts/gen-tokens.mjs takes it out of the
 * stylesheet), with the grain's colour taken from uv-deep rather than from
 * the file: the file says where, the token says what colour.
 *
 * A dot is the gradient sampled at pixel centres, as a browser samples it:
 * full bg-dot within 1px of a dot's centre, nothing past 1.2px. At 1x that is
 * a 2 x 2 block on each grid point, which is what the web editors show.
 *
 * Only on the window ground, never inside a panel or a well. The animated
 * Ground draws over this and, at rest, looks like it.
 */
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace uv::ground
{

/* The ground's period, in px: the grain tile, which the dot pitch divides. */
inline constexpr int tileSize = 96;

/* The 96 x 96 picture the ground repeats. Built on first use and kept until
 * JUCE shuts down. Message thread. */
const juce::Image& tile();

/*
 * Fills `area` with the ground, its tiles starting at `origin` -- the
 * window's top-left in `g`'s coordinates -- so a component that paints part
 * of the ground lines its dots up with the window's.
 */
void paint (juce::Graphics&, juce::Rectangle<float> area, juce::Point<float> origin = {});

} // namespace uv::ground

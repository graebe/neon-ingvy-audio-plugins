// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The only light Ultraviolet allows: a violet halo around a white core.
 *
 * "No drop shadows: nothing floats. The only shadow is light." Three of them,
 * and nothing else may glow:
 *
 *   glowLed    a lit step, an LED, the playhead, a primary button, a lit tab:
 *              the token glow-led, 10px uv-deep and 2px uv
 *   glowFocus  keyboard focus on any control: the token glow-focus, a 1px uv
 *              ring and an 8px uv-glow halo
 *   glowArc    under a lit STROKE -- a knob's value arc, a plot's curve --
 *              where a 10px LED halo on a 2px line would be mud: 3px uv-deep
 *
 * TWO HALOES, AND THE ORDER IS THE POINT: the wide one is the saturated violet,
 * which is where the hue lives now that the fill is near white; the tight one
 * is the core colour, which keeps the element's own edge from being eaten by
 * the violet.
 *
 * DRAWN AS THE WEB EDITORS DRAW THEM, because the native ones replace them and
 * must look the same. That means two different blurs, and the difference is
 * not a detail:
 *
 *   glow-focus is a CSS box-shadow, whose blur is a RADIUS: a Gaussian of
 *   standard deviation r / 2. shadow() is that.
 *
 *   glow-led and the arc glow are CSS filters, drop-shadow(), in the web kit
 *   (tokens.css --glow-led, --glow-arc) and in the design's own knob arc --
 *   and a drop-shadow's length IS the standard deviation (Filter Effects 1:
 *   "the standard deviation instead of blur radius"), twice as wide as the
 *   same number in a box-shadow. Two filters in a row also compound: the
 *   second shadows the first's result, so glow-led's white halo lies under
 *   its violet one as well as under the element. glowLed() and glowArc() do
 *   exactly that, blurring as browsers do (SVG's three box blurs).
 *
 * WHERE IT DIFFERS FROM THE TOKEN, ON PURPOSE: glow-led's colours are drawn at
 * 0.55 alpha, not 1, as the web kit's tokens.css drew them -- the token
 * test's one exemption ("a judgement about a blur, not a typo",
 * tests/site_tokens.test.mjs); the arc glow at 0.45.
 *
 * Call each BEFORE painting the element it surrounds: a halo lies under its
 * element. glowFocus is the exception that needs no order -- it paints only
 * outside the element, as a box-shadow does.
 */
#pragma once

#include "UvTokens.h"

#include <juce_graphics/juce_graphics.h>

namespace uv::light
{

/* glow-led's strength in the web kit (tokens.css --glow-led); see above. */
inline constexpr float ledOpacity = 0.55f;
/* --glow-arc: drop-shadow(0 0 3px uv-deep) at 0.45. */
inline constexpr float arcDeviation = 3.0f;
inline constexpr float arcOpacity = 0.45f;

/* The halo of a lit rectangle (a step, a tab, a button) -- kept per size, so
 * a grid of lit steps costs one blur -- or of any lit shape (a wedge, a
 * dot, an LED's lens). */
void glowLed (juce::Graphics&, juce::Rectangle<float> element);
void glowLed (juce::Graphics&, const juce::Path& element);

/*
 * The keyboard's focus ring around `element`: its 1px uv ring and its 8px
 * uv-glow halo, OUTSIDE the element only, as CSS draws a box-shadow -- so a
 * control with a transparent middle shows no halo through itself.
 * `cornerRadius` follows a round control's outline (a knob's disc: half its
 * size).
 */
void glowFocus (juce::Graphics&, juce::Rectangle<float> element, float cornerRadius = 0.0f);

/* The soft halo under a lit stroke: `stroke` is the stroke's outline, as
 * juce::PathStrokeType::createStrokedPath makes it. */
void glowArc (juce::Graphics&, const juce::Path& stroke);

/*
 * One layer of a CSS box-shadow -- offsets, blur RADIUS and spread in px, and
 * a colour -- under `shape`, its alpha multiplied by `opacity`: for a light
 * the design states as a box-shadow (a meter's fill, a warning LED). Drawn
 * everywhere, inside the shape too: paint the shape over it.
 */
void shadow (juce::Graphics&, juce::Rectangle<float> shape, const tok::ShadowLayer&,
             float opacity = 1.0f);
void shadow (juce::Graphics&, const juce::Path& shape, const tok::ShadowLayer&,
             float opacity = 1.0f);

/*
 * One CSS drop-shadow() filter -- the layer's blur read as the standard
 * deviation -- under `shape`. A chain of filters is not a list of these:
 * glowLed() is the chain glow-led is.
 */
void dropShadow (juce::Graphics&, const juce::Path& shape, const tok::ShadowLayer&,
                 float opacity = 1.0f);

} // namespace uv::light

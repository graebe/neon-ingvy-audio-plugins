// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The only light Ultraviolet allows: a violet halo around a white core.
 *
 * "No drop shadows: nothing floats. The only shadow is light." Three of them,
 * and nothing else may glow:
 *
 *   glowLed    tokens glow-led, 0 0 10px uv-deep, 0 0 2px uv -- a lit step,
 *              an LED, the playhead, a primary button, a lit tab
 *   glowFocus  tokens glow-focus, 0 0 0 1px uv, 0 0 8px uv-glow -- keyboard
 *              focus on any control
 *   glowArc    the kit's drop-shadow(0 0 3px uv-deep) at 0.45 -- under a lit
 *              STROKE (a knob's value arc, a plot's curve), where a 10px LED
 *              halo on a 2px line would be mud
 *
 * TWO HALOES, AND THE ORDER IS THE POINT: the wide one is the saturated violet,
 * which is where the hue lives now that the fill is near white; the tight one
 * is the core colour, and keeps the element's own edge from being eaten by the
 * violet.
 *
 * DRAWN AS CSS DRAWS THEM. A CSS blur radius r is a Gaussian of standard
 * deviation r / 2, and so is this: a rectangle's glow is computed exactly (a
 * blurred rectangle is two error functions, one per axis), any other shape is
 * rendered as a mask and blurred. The JUCE editor drew concentric strokes
 * instead, a guess at the falloff the web kit later had to match by eye.
 *
 * WHERE IT DIFFERS FROM THE TOKEN, ON PURPOSE: glow-led is drawn at 0.55 of its
 * colours' alpha, not 1. tokens.css draws it at 0.55 too, and the ui-kit's
 * token test carries that as its one exemption ("a judgement about a blur,
 * not a typo"); the native editors look like the web ones, so they keep it.
 *
 * Call each BEFORE painting the element it surrounds: the halo is under the
 * element, as a drop-shadow is.
 */
#pragma once

#include "UvTokens.h"

#include <juce_graphics/juce_graphics.h>

namespace uv::light
{

/* glow-led's strength in the web kit (tokens.css --glow-led); see above. */
inline constexpr float ledOpacity = 0.55f;
/* --glow-arc: drop-shadow(0 0 3px uv-deep) at 0.45. */
inline constexpr float arcBlur = 3.0f;
inline constexpr float arcOpacity = 0.45f;

/* The halo of a lit rectangle (a step, a tab, a button), or of any lit shape
 * (a wedge, a dot, an LED's lens). */
void glowLed (juce::Graphics&, juce::Rectangle<float> element);
void glowLed (juce::Graphics&, const juce::Path& element);

/*
 * The keyboard's focus ring around `element`: its 1px uv ring and its 8px
 * uv-glow halo, drawn OUTSIDE the element only, as CSS draws a box-shadow --
 * so a control with a transparent middle shows no halo through itself.
 * `cornerRadius` follows a round control's outline (a knob's disc: half its
 * size).
 */
void glowFocus (juce::Graphics&, juce::Rectangle<float> element, float cornerRadius = 0.0f);

/* The soft halo under a lit stroke: `stroke` is the stroke's outline, as
 * juce::PathStrokeType::createStrokedPath makes it. */
void glowArc (juce::Graphics&, const juce::Path& stroke);

/*
 * The general case, which the three are made of: one CSS shadow layer -- its
 * offsets, blur and spread in px, and its colour -- under `shape`, its alpha
 * multiplied by `opacity`.
 */
void shadow (juce::Graphics&, juce::Rectangle<float> shape, const tok::ShadowLayer&,
             float opacity = 1.0f);
void shadow (juce::Graphics&, const juce::Path& shape, const tok::ShadowLayer&,
             float opacity = 1.0f);

} // namespace uv::light

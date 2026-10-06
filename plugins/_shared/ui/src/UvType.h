// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Ultraviolet's type in JUCE: the two embedded faces of JetBrains Mono, and the
 * system's six text styles as juce::Fonts that measure what CSS measures.
 *
 * EMBEDDED FACES ONLY. Every font the kit makes carries one of the two faces
 * in fonts/ (BinaryData), never a family name for the system to resolve: a
 * name is looked up per machine, and a machine without the face substitutes
 * one, so the same editor would set different text on two computers and no
 * snapshot could hold it. uv::LookAndFeel maps JUCE's own default fonts onto
 * the same faces, for the widgets that make a font of their own.
 *
 * CSS SIZES. The system states sizes in CSS px, and a CSS font-size is the em
 * square; a JUCE height is ascent plus descent, 1.32 em for this face. So a
 * style's size goes to FontOptions::withPointHeight, which is the em square,
 * and `value` (13px) sets the same glyphs here as in the web editors. The
 * JUCE editor before them used withHeight and drew its text a quarter
 * smaller than the design.
 *
 * TRACKING is CSS letter-spacing in em; JUCE's extra kerning factor is a
 * multiple of the JUCE height, so the factor is tracking x size / height.
 *
 * UPPERCASE is CSS text-transform, which a token cannot carry: `title` and
 * `label` are set in capitals (the README's Type section), and cased() does
 * it for them.
 */
#pragma once

#include "UvTokens.h"

#include <juce_graphics/juce_graphics.h>

#include <utility>

namespace uv
{

/* The two faces the six styles use: weight 400 and weight 500. */
struct Fonts
{
    juce::Typeface::Ptr regular;
    juce::Typeface::Ptr medium;

    /* The face for a CSS weight: 500 and heavier is Medium, the rest Regular.
     * The system uses no other weight, and bundles no other face. */
    juce::Typeface::Ptr forWeight (int cssWeight) const;
};

/*
 * The faces, made from BinaryData the first time they are asked for.
 *
 * Held by a juce::DeletedAtShutdown singleton, so they are released while
 * JUCE shuts down and not after it: a Typeface released from a static
 * destructor at process exit takes a lock JUCE has already destroyed, and the
 * process aborts on its way out (the JUCE editor hit exactly that). Message
 * thread.
 */
const Fonts& fonts();

namespace type
{
/* A style's font: its face, its size as the em square, its tracking. */
juce::Font font (const tok::TextStyle&);

/* The six, by name. */
juce::Font readout();
juce::Font title();
juce::Font label();
juce::Font value();
juce::Font button();
juce::Font hint();

/* Whether a style is set in capitals: title and label. */
bool isUpper (const tok::TextStyle&);

/* `text` as `style` sets it: in capitals for title and label, as given for
 * the rest. */
juce::String cased (const tok::TextStyle&, const juce::String& text);

/* The width `text` takes in `font`, tracking included. */
float width (const juce::Font&, const juce::String& text);

/*
 * One line of text the way CSS sets it in a line box: `box` is the line box
 * (a style's lineHeight tall, or a control's height), the glyphs centred in
 * it vertically, and `just` placing them horizontally. A line wider than the
 * box is cut with an ellipsis, as the system's single-line text is
 * (text-overflow: ellipsis), never wrapped and never shrunk.
 */
void draw (juce::Graphics&, const juce::String& text, juce::Rectangle<float> box,
           const juce::Font&, juce::Colour, juce::Justification just = juce::Justification::centredLeft);

/*
 * A value with its unit, as the system prints one: the number in `ink`, the
 * unit in `ink-muted` 2px after it, split at the LAST space ("40.0 ms",
 * "90 %"). Text with no space is all number ("1/16", "1.00"), which is what
 * the system wants for a unitless value; splitting at the last space rather
 * than the first keeps "1 / 16 T" whole rather than calling "16 T" a unit.
 * Centred in `box` in the `value` style; disabled, both parts are `ink-dim`.
 * The Readout's text, and a slider's text box's.
 */
void drawValueWithUnit (juce::Graphics&, const juce::String& text, juce::Rectangle<float> box,
                        bool enabled = true);

/* The two halves drawValueWithUnit draws: { number, unit }. */
std::pair<juce::String, juce::String> splitUnit (const juce::String& text);
} // namespace type

} // namespace uv

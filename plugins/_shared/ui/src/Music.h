// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the kit's music views share: a set of MIDI notes, the black keys, and
 * Bravura, the notation font.
 *
 * BRAVURA IS DRAWN BY SMuFL's RULES. The font's em is four staff spaces, so a
 * staff whose lines are `space` apart sets Bravura at an em of 4 x space; and
 * every glyph's origin is where SMuFL puts it -- a notehead's and an
 * accidental's on the line or space the note sits on, a G clef's on the G
 * line, an F clef's on the F line. drawGlyph() takes that point, so a caller
 * places a glyph by the staff position it means and never by its bounding box.
 *
 * Embedded, as JetBrains Mono is (fonts/bravura/README.md): the same notation
 * on every machine of an OS, so a snapshot can hold it.
 */
#pragma once

#include <juce_graphics/juce_graphics.h>

#include <bitset>

namespace ni::ui::music
{

/* MIDI notes 0 to 127, one bit each. */
using NoteSet = std::bitset<128>;

/* Whether a MIDI note is a black key. */
constexpr bool isBlack (int midi) noexcept
{
    const int pc = ((midi % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

/* SMuFL code points of the glyphs the kit draws. */
namespace glyph
{
inline constexpr juce::juce_wchar gClef = 0xE050;
inline constexpr juce::juce_wchar fClef = 0xE062;
inline constexpr juce::juce_wchar noteheadBlack = 0xE0A4;
inline constexpr juce::juce_wchar flat = 0xE260;
inline constexpr juce::juce_wchar natural = 0xE261;
inline constexpr juce::juce_wchar sharp = 0xE262;
inline constexpr juce::juce_wchar doubleSharp = 0xE263;
inline constexpr juce::juce_wchar doubleFlat = 0xE264;

/* The accidental for -2 to +2: double flat to double sharp, 0 a natural. */
juce::juce_wchar accidental (int alteration) noexcept;
} // namespace glyph

/* Bravura, made from BinaryData the first time it is asked for, and held
 * until JUCE shuts down (UvType.h says why not longer). Message thread. */
juce::Typeface::Ptr bravura();

/* Bravura for a staff whose lines are `space` px apart. */
juce::Font bravuraFont (float space);

/* One glyph with its SMuFL origin at `origin`. */
void drawGlyph (juce::Graphics&, juce::juce_wchar, juce::Point<float> origin, float space, juce::Colour);

/* A glyph's advance width at `space`. */
float glyphWidth (juce::juce_wchar, float space);

} // namespace ni::ui::music

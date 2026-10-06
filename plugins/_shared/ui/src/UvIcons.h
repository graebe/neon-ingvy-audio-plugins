// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The system's fifteen machine glyphs, as juce::Drawables in a colour.
 *
 * NOT COPIED, EMBEDDED. The SVGs are the design mirror's own files
 * (design/scheme/project/assets/Icons), compiled into ni_ui_assets as they
 * are; a re-synced icon arrives with the next build. The README there: a 16px
 * grid, a 1.5px stroke with square caps and mitred joins, single-ink, and
 * `play` and `record` filled where the rest are stroke only.
 *
 * currentColor, AS THE WEB KIT HAS IT. A glyph takes the colour of the
 * control it sits on -- `ink` at rest, `on-uv` on a lit button, `bg-000` on
 * the red record button, `ink-dim` disabled -- so it is asked for in that
 * colour: every stroke the file draws, and the fill of the two filled glyphs,
 * are set to it. The file's own ink (#f3ecff) is never drawn.
 *
 * Use one only where it replaces a verb on a control that acts, and never
 * one outside the set (the README's Iconography): an unknown name is a
 * programming error, asserted, and draws nothing.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace uv
{

/* The glyphs the set has, in the order the system lists them: play, pause,
 * stop, record, loop, copy, paste, export, export-all, import, shuffle,
 * reset, link, chevron, power. */
const juce::StringArray& iconNames();

bool hasIcon (const juce::String& name);

/* Whether the system draws `name` filled rather than stroked. */
bool isFilledIcon (const juce::String& name);

/*
 * A new Drawable of the glyph in `colour`, on the glyph's 16 x 16 grid (its
 * drawable bounds are 0, 0, 16, 16). Keep it, and call tint() when the
 * control's state changes; making one per paint parses nothing (the files
 * are parsed once) but still copies a small tree. nullptr for a name outside
 * the set. Message thread.
 */
std::unique_ptr<juce::Drawable> icon (const juce::String& name, juce::Colour colour);

/* Recolours a glyph made by icon(), in place. */
void tint (juce::Drawable& glyph, const juce::String& name, juce::Colour colour);

/*
 * Draws the glyph at 16 x 16, centred in `area` -- the 16px glyph in its
 * 28px control. For a paint routine that has no Drawable to keep; the
 * coloured copies are cached by name and colour.
 */
void drawIcon (juce::Graphics&, const juce::String& name, juce::Colour colour,
               juce::Rectangle<float> area);

} // namespace uv

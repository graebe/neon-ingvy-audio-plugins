// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A piano keyboard standing up the side of a pitch view: one key per
 * semitone row, lined up with the rows of any view whose vertical axis is
 * MIDI pitch (PitchView, NoteBars -- docs/adr/0006), the highest pitch at the
 * top, its fronts facing the view.
 *
 * THE KEYS LINE UP WITH THE ROWS. The axis is the view's own zoom::Axis
 * (setAxis), mapped exactly as PitchView maps it: a pitch at
 * (end - midi) / span of the height from the top, its row half a semitone
 * either side. So a key follows every pan and zoom of the view it stands
 * beside, given the same axis and the same height.
 *
 *   black key   exactly its row, from the back edge (the left) across
 *               KeyPaint's 60 % of the width
 *   white key   its row and half of each black row next to it, the full
 *               width, a 1px gap on its upper side, as the Keyboard has it
 *               on its right; on rows under 4px, no gap
 *   lit         uv (uv-deep dimmed), as the Keyboard; KeyPaint draws both
 *   each C      its name in the C row, PitchView's names (C3 is 60), in the
 *               hint style, ink-dim, right-aligned before the marks; a row
 *               too thin to hold it (PitchView::minNamedRow) goes unnamed
 *   scale       a 4px dot at the key's front, on its row: ink-dim for a key
 *               of the scale, ink for the tonic, on-uv on a lit key; none
 *               on a row under 5px, and none without a scale
 *
 * DISPLAY ONLY, as the Keyboard: no pointer and no keyboard focus, so a
 * press beside a view goes to whatever is under the strip. Its accessible
 * description is its owner's to set, in the owner's words. It paints
 * nothing where there is no key -- past MIDI's 0 and 127, and the gaps -- so
 * the well it stands in shows there.
 */
#pragma once

#include "Music.h"
#include "ZoomMath.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>

namespace ni::ui
{

class PianoAxis : public juce::Component
{
public:
    /* The strip beside a view: as wide as a ToolColumn's. */
    static constexpr int defaultWidth = 32;
    /* A scale mark's diameter, and the thinnest row that carries one. */
    static constexpr float markSize = 4.0f;
    static constexpr float minMarkedRow = 5.0f;
    /* The thinnest row whose white keys keep their gap. */
    static constexpr float minGappedRow = 4.0f;

    PianoAxis();
    ~PianoAxis() override;

    /* The view's pitch axis, in MIDI pitch: the view's start the lowest
     * pitch shown. Repaints. */
    void setAxis (const zoom::Axis&);
    const zoom::Axis& getAxis() const noexcept { return axis; }

    /* The keys lit, and whether they are dimmed (kept, not sounding). */
    void setLit (const music::NoteSet&);
    const music::NoteSet& getLit() const noexcept { return lit; }
    void setDimmed (bool);
    bool isDimmed() const noexcept { return dimmed; }

    /* The scale's pitch classes, C at bit 0, and its tonic as a pitch class
     * (-1 for none). 0 marks nothing, which is the default. */
    void setScale (std::uint16_t pitchClasses, int tonic = -1);
    std::uint16_t getScale() const noexcept { return scale; }
    int getTonic() const noexcept { return tonic; }

    /* Pitch to y in this component, and back: PitchView's mapping. */
    float yOf (double midi) const noexcept;
    double pitchAt (float y) const noexcept;

    /* A semitone's row, and its key as drawn (a white key's gap left out);
     * empty for a pitch outside MIDI's range. */
    juce::Rectangle<float> rowBounds (int midi) const;
    juce::Rectangle<float> keyBounds (int midi) const;
    /* Where a key's scale mark sits, whether or not it is in the scale;
     * empty where none is drawn (a row too thin, a pitch outside MIDI). */
    juce::Rectangle<float> markBounds (int midi) const;

    void paint (juce::Graphics&) override;

private:
    float rowHeight() const noexcept;

    zoom::Axis axis;
    music::NoteSet lit;
    bool dimmed = false;
    std::uint16_t scale = 0;
    int tonic = -1;

    JUCE_DECLARE_NON_COPYABLE (PianoAxis)
};

} // namespace ni::ui

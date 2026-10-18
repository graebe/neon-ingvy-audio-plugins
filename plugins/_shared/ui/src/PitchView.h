// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A pitch axis over a well that pans (docs/adr/0006): the rows a note editor
 * is drawn on, one per semitone, and their names in the gutter at the left.
 *
 * THE ROWS: a row of the scale on bg-200, any other on bg-100, each with a hairline along its
 * top -- line-200 above a C, line-100 elsewhere -- across the whole width.
 * The gutter, `gutter` wide, names each row in the hint style at 6px from the
 * left, ink-muted for a row of the scale and ink-dim otherwise; a row too
 * thin to hold its name goes unnamed.
 *
 * THE AXIS is in MIDI pitch, its start the lowest pitch shown: the view's
 * highest pitch is at the top. A drag in the gutter pans (onPan with the new
 * start), as on any ruler; a double-click there asks for the view to fit
 * what it shows (onFit). It moves nothing itself: its owner sets the axis.
 */
#pragma once

#include "ZoomMath.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace ni::ui
{

class PitchView : public juce::Component
{
public:
    static constexpr int gutter = 44;
    /* The thinnest row that is still named, pixels. */
    static constexpr float minNamedRow = 10.0f;

    /* A pitch's name in Live's numbering: C3 is 60. */
    static juce::String nameOf (int midi);
    /* The pitch a name means, as nameOf() writes it or with a flat ("Eb3",
     * "d#2", "C-1"); nothing for anything else. */
    static std::optional<int> midiOf (const juce::String& name);

    PitchView();
    ~PitchView() override;

    void setAxis (const zoom::Axis&);
    const zoom::Axis& getAxis() const noexcept { return axis; }

    /* The scale's pitch classes, C at bit 0; all twelve by default. */
    void setScale (std::uint16_t pitchClasses);
    std::uint16_t getScale() const noexcept { return scale; }

    /* Pitch to y in this component, and back. */
    float yOf (double midi) const noexcept;
    double pitchAt (float y) const noexcept;

    std::function<void (double start)> onPan;
    std::function<void()> onFit;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    zoom::Axis axis;
    std::uint16_t scale = 0x0fff;
    std::optional<double> pressStart;

    JUCE_DECLARE_NON_COPYABLE (PitchView)
};

} // namespace ni::ui

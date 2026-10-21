// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The piano up a pitch view's side. PianoAxis.h has its keys and rules.
 */
#include "PianoAxis.h"

#include "KeyPaint.h"
#include "PitchView.h"
#include "UvTokens.h"

#include <algorithm>
#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

/* The marks' column: this far in from a key's front edge to the dot's
 * centre, and the room a C's name leaves for it. */
constexpr float markInset = 5.0f;
constexpr float nameRight = 10.0f;
constexpr float nameLeft = 2.0f;

bool inRange (int midi) noexcept
{
    return midi >= 0 && midi < 128;
}
} // namespace

PianoAxis::PianoAxis()
{
    setInterceptsMouseClicks (false, false);
    setWantsKeyboardFocus (false);
    setTitle ("Piano");
    setSize (defaultWidth, 240);
}

PianoAxis::~PianoAxis() = default;

void PianoAxis::setAxis (const zoom::Axis& a)
{
    axis = a;
    repaint();
}

void PianoAxis::setLit (const music::NoteSet& notes)
{
    if (notes == lit)
        return;
    lit = notes;
    repaint();
}

void PianoAxis::setDimmed (bool shouldBe)
{
    if (shouldBe == dimmed)
        return;
    dimmed = shouldBe;
    repaint();
}

void PianoAxis::setScale (std::uint16_t pitchClasses, int tonicClass)
{
    const auto pcs = (std::uint16_t) (pitchClasses & 0x0fffu);
    const int t = tonicClass >= 0 && tonicClass < 12 ? tonicClass : -1;
    if (pcs == scale && t == tonic)
        return;
    scale = pcs;
    tonic = t;
    repaint();
}

float PianoAxis::yOf (double midi) const noexcept
{
    return (float) ((axis.end() - midi) / axis.span * (double) getHeight());
}

double PianoAxis::pitchAt (float y) const noexcept
{
    return axis.end() - (double) y / std::max (1.0, (double) getHeight()) * axis.span;
}

float PianoAxis::rowHeight() const noexcept
{
    return (float) ((double) getHeight() / axis.span);
}

juce::Rectangle<float> PianoAxis::rowBounds (int midi) const
{
    if (! inRange (midi))
        return {};
    const float top = yOf ((double) midi + 0.5);
    return { 0.0f, top, (float) getWidth(), yOf ((double) midi - 0.5) - top };
}

juce::Rectangle<float> PianoAxis::keyBounds (int midi) const
{
    if (! inRange (midi))
        return {};
    const auto row = rowBounds (midi);
    if (music::isBlack (midi))
        return row.withWidth ((float) getWidth() * keypaint::blackShare);

    /* Half of each black row beside it is this key's too. */
    const float top = music::isBlack (midi + 1) ? yOf ((double) midi + 1.0) : row.getY();
    const float bottom = music::isBlack (midi - 1) ? yOf ((double) midi - 1.0) : row.getBottom();
    juce::Rectangle<float> key { 0.0f, top, (float) getWidth(), bottom - top };
    if (rowHeight() >= minGappedRow)
        key.removeFromTop (keypaint::gap);
    return key;
}

juce::Rectangle<float> PianoAxis::markBounds (int midi) const
{
    if (! inRange (midi) || rowHeight() < minMarkedRow)
        return {};
    const auto row = rowBounds (midi);
    const float front = music::isBlack (midi) ? (float) getWidth() * keypaint::blackShare : (float) getWidth();
    return juce::Rectangle<float> (markSize, markSize).withCentre ({ front - markInset, row.getCentreY() });
}

void PianoAxis::paint (juce::Graphics& g)
{
    const auto litColour = keypaint::litColour (dimmed);
    const int lo = std::max (0, (int) std::floor (axis.start - 1.0));
    const int hi = std::min (127, (int) std::ceil (axis.end() + 1.0));
    const bool named = rowHeight() >= PitchView::minNamedRow;
    const auto& hint = uv::tok::type::hint;

    /* The white keys first, then the black ones over them; each key's mark
     * and name with it. */
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int midi = lo; midi <= hi; ++midi)
        {
            if (music::isBlack (midi) != (pass == 1))
                continue;
            const bool on = lit.test ((size_t) midi);
            const auto key = keyBounds (midi);
            if (pass == 0)
                keypaint::whiteKey (g, key, on, litColour);
            else
                keypaint::blackKey (g, key, on, litColour);

            const int pc = midi % 12;
            if (((scale >> pc) & 1u) != 0)
                if (const auto mark = markBounds (midi); ! mark.isEmpty())
                {
                    g.setColour (on ? c::onUv : pc == tonic ? c::ink : c::inkDim);
                    g.fillEllipse (mark);
                }

            if (pc == 0 && named)
            {
                const auto row = rowBounds (midi);
                const juce::Rectangle<float> box { nameLeft, row.getCentreY() - hint.lineHeight * 0.5f,
                                                   (float) getWidth() - nameLeft - nameRight, hint.lineHeight };
                keypaint::keyName (g, PitchView::nameOf (midi), box, on, juce::Justification::centredRight);
            }
        }
    }
}

} // namespace ni::ui

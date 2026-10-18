// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The pitch axis. PitchView.h has its rows and rules.
 */
#include "PitchView.h"

#include "UvTokens.h"
#include "UvType.h"

#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;
}

juce::String PitchView::nameOf (int midi)
{
    static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int pc = ((midi % 12) + 12) % 12;
    const int octave = (int) std::floor ((double) midi / 12.0) - 2;
    return juce::String (names[pc]) + juce::String (octave);
}

std::optional<int> PitchView::midiOf (const juce::String& name)
{
    static constexpr int naturals[] { 9, 11, 0, 2, 4, 5, 7 };   // A to G
    const auto t = name.trim();
    if (t.isEmpty())
        return std::nullopt;
    const auto letter = juce::CharacterFunctions::toUpperCase (t[0]);
    if (letter < 'A' || letter > 'G')
        return std::nullopt;
    int pc = naturals[letter - 'A'];
    auto rest = t.substring (1);
    if (rest.startsWith ("#") || rest.startsWith (juce::String::fromUTF8 ("\xe2\x99\xaf")))
        ++pc;
    else if (rest.startsWith ("b") || rest.startsWith (juce::String::fromUTF8 ("\xe2\x99\xad")))
        --pc;
    if (pc != naturals[letter - 'A'])
        rest = rest.substring (1);
    rest = rest.replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-");
    if (rest.isEmpty() || ! rest.trimCharactersAtStart ("-").containsOnly ("0123456789") || rest.lastIndexOfChar ('-') > 0)
        return std::nullopt;
    const int midi = (rest.getIntValue() + 2) * 12 + pc;
    if (midi < 0 || midi > 127)
        return std::nullopt;
    return midi;
}

PitchView::PitchView()
{
    setTitle ("Pitch");
}

PitchView::~PitchView() = default;

void PitchView::setAxis (const zoom::Axis& a)
{
    axis = a;
    repaint();
}

void PitchView::setScale (std::uint16_t pcs)
{
    scale = pcs;
    repaint();
}

float PitchView::yOf (double midi) const noexcept
{
    return (float) ((axis.end() - midi) / axis.span * (double) getHeight());
}

double PitchView::pitchAt (float y) const noexcept
{
    return axis.end() - (double) y / std::max (1.0, (double) getHeight()) * axis.span;
}

void PitchView::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const auto& style = uv::tok::type::hint;
    const auto font = uv::type::font (style);
    const int lo = (int) std::floor (axis.start - 0.5);
    const int hi = (int) std::ceil (axis.end() + 0.5);
    for (int m = lo; m <= hi; ++m)
    {
        const int pc = ((m % 12) + 12) % 12;
        const bool inScale = ((scale >> pc) & 1) != 0;
        const float top = yOf ((double) m + 0.5);
        const float bottom = yOf ((double) m - 0.5);
        const juce::Rectangle<float> row { 0.0f, top, w, bottom - top };
        g.setColour (inScale ? c::bg200 : c::bg100);
        g.fillRect (row);
        g.setColour (pc == 0 ? c::line200 : c::line100);
        g.fillRect (row.withHeight (uv::tok::stroke::strokeHair));
        if (row.getHeight() >= minNamedRow)
            uv::type::draw (g, nameOf (m), { 6.0f, row.getCentreY() - style.lineHeight * 0.5f, (float) gutter, style.lineHeight },
                            font, inScale ? c::inkMuted : c::inkDim);
    }
}

void PitchView::mouseDown (const juce::MouseEvent& e)
{
    pressStart = e.position.x < (float) gutter ? std::optional<double> (axis.start) : std::nullopt;
}

void PitchView::mouseDrag (const juce::MouseEvent& e)
{
    if (pressStart.has_value() && onPan)
        onPan (*pressStart + (double) e.getDistanceFromDragStartY() * axis.span / std::max (1.0, (double) getHeight()));
}

void PitchView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.x < (float) gutter && onFit)
        onFit();
}

} // namespace ni::ui

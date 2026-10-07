// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The keyboard. Keyboard.h has its looks.
 */
#include "Keyboard.h"

#include "UvTokens.h"
#include "UvType.h"

#include <algorithm>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

/* The white keys of an octave, as semitones above its C. */
constexpr int whites[] = { 0, 2, 4, 5, 7, 9, 11 };

/* Which white key a note is, or the white key a black one sits after. */
int whiteIndex (int midi)
{
    const int octave = midi / 12;
    const int pc = midi % 12;
    int i = 6;
    while (whites[i] > pc)
        --i;
    return octave * 7 + i;
}

constexpr float blackShare = 0.6f;
constexpr float gap = 1.0f;
constexpr float markHeight = 3.0f;
} // namespace

Keyboard::Keyboard()
{
    setInterceptsMouseClicks (false, false);
    setWantsKeyboardFocus (false);
    setTitle ("Keyboard");
}

void Keyboard::setState (const State& next)
{
    if (next == state)
        return;
    state = next;
    repaint();
}

float Keyboard::whiteWidth() const
{
    return (float) getWidth() / (float) (7 * std::max (1, state.octaves));
}

float Keyboard::keysHeight() const
{
    return std::max (0.0f, (float) getHeight() - markSpace);
}

bool Keyboard::shows (int midi) const
{
    return midi >= state.lowest && midi < state.lowest + 12 * state.octaves && midi < 128;
}

juce::Rectangle<float> Keyboard::keyBounds (int midi) const
{
    if (! shows (midi))
        return {};
    const float w = whiteWidth();
    const float x = (float) (whiteIndex (midi) - whiteIndex (state.lowest)) * w;
    if (music::isBlack (midi))
    {
        const float bw = w * blackShare;
        return { x + w - bw * 0.5f, 0.0f, bw, keysHeight() * blackShare };
    }
    return { x, 0.0f, w - gap, keysHeight() };
}

int Keyboard::lowestToShow (const music::NoteSet& notes, int octaves, int current)
{
    if (notes.none())
        return current;
    int low = 0;
    while (! notes.test ((size_t) low))
        ++low;
    int high = 127;
    while (! notes.test ((size_t) high))
        --high;

    const int span = 12 * std::max (1, octaves);
    if (low >= current && high < current + span)
        return current;
    /* The highest C whose octaves still reach 127 -- G9, MIDI's top. */
    const int top = std::max (0, 120 - (span - 12));
    return std::clamp (low / 12 * 12, 0, top);
}

void Keyboard::paint (juce::Graphics& g)
{
    const auto lit = state.dimmed ? c::uvDeep : c::uv;
    const auto& hint = uv::tok::type::hint;
    const auto font = uv::type::font (hint);

    /* The white keys first, then the black ones over them. */
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int midi = state.lowest; midi < state.lowest + 12 * state.octaves && midi < 128; ++midi)
        {
            if (music::isBlack (midi) != (pass == 1))
                continue;
            const auto key = keyBounds (midi);
            const bool on = state.lit.test ((size_t) midi);

            if (pass == 0)
            {
                g.setColour (on ? lit : c::bg300);
                g.fillRect (key);
                if (midi % 12 == 0)
                {
                    const auto label = "C" + juce::String (midi / 12 - 1);
                    uv::type::draw (g, label, key.withTrimmedTop (key.getHeight() - hint.lineHeight - 2.0f).withTrimmedBottom (2.0f),
                                    font, on ? c::onUv : c::inkDim, juce::Justification::centred);
                }
            }
            else
            {
                g.setColour (on ? lit : c::bg000);
                g.fillRect (key);
                g.setColour (on ? lit : c::line200);
                g.drawRect (key, uv::tok::stroke::strokeHair);
            }
        }
    }

    if (state.bass >= 0 && shows (state.bass))
    {
        const auto key = keyBounds (state.bass);
        g.setColour (lit);
        g.fillRect (juce::Rectangle<float> (key.getX() + 3.0f, keysHeight() + markSpace - markHeight,
                                            std::max (2.0f, key.getWidth() - 6.0f), markHeight));
    }
}

} // namespace ni::ui

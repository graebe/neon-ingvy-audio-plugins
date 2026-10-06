// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The chord readout. ChordReadout.h has its rows.
 */
#include "ChordReadout.h"

#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;
namespace sp = uv::tok::space;

constexpr float hair = uv::tok::stroke::strokeHair;
constexpr float chipH = 24.0f;
constexpr float rowLabelW = 48.0f;
constexpr float lens = 8.0f;

/* Row tops: the name, its words, the notes, the other readings. */
constexpr float nameTop = 0.0f;
constexpr float wordsTop = 40.0f;
constexpr float notesTop = 80.0f;
constexpr float alsoTop = 108.0f;

/* A chip: text in a hairline box, `space2` either side. Returns its width. */
float chip (juce::Graphics& g, const juce::String& text, float x, float y, juce::Colour fill, juce::Colour edge,
            juce::Colour ink)
{
    const auto font = uv::type::font (uv::tok::type::value);
    const float w = uv::type::width (font, text) + 2.0f * sp::space2;
    const juce::Rectangle<float> box { x, y, w, chipH };
    if (! fill.isTransparent())
    {
        g.setColour (fill);
        g.fillRect (box);
    }
    g.setColour (edge);
    g.drawRect (box, hair);
    uv::type::draw (g, text, box, font, ink, juce::Justification::centred);
    return w;
}
} // namespace

ChordReadout::ChordReadout()
{
    setInterceptsMouseClicks (false, false);
    setTitle ("Chord");
}

void ChordReadout::setState (const State& next)
{
    if (next == state)
        return;
    state = next;
    setDescription (state.name.isEmpty() ? juce::String ("Nothing is sounding")
                                         : state.name + ", " + state.description);
    repaint();
}

void ChordReadout::paint (juce::Graphics& g)
{
    const float width = (float) getWidth();
    const auto& readout = uv::tok::type::readout;
    const auto& label = uv::tok::type::label;
    const auto& value = uv::tok::type::value;

    /* The name, and the numeral beside it. */
    const bool empty = state.name.isEmpty();
    const auto nameFont = uv::type::font (readout);
    const auto name = empty ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : state.name;
    const float nameW = uv::type::width (nameFont, name);
    uv::type::draw (g, name, { 0.0f, nameTop, nameW + 1.0f, readout.lineHeight }, nameFont, empty ? c::inkDim : c::ink);
    if (state.degree.isNotEmpty())
        chip (g, state.degree, nameW + sp::space4, nameTop + (readout.lineHeight - chipH) * 0.5f, c::bg200, c::line200, c::uv);

    if (state.held)
    {
        const auto& labelFont = uv::type::font (label);
        const auto word = uv::type::cased (label, "Held");
        const float wordW = uv::type::width (labelFont, word);
        const float x = width - wordW - sp::space2 - lens;
        const juce::Rectangle<float> led { x, nameTop + (readout.lineHeight - lens) * 0.5f, lens, lens };
        juce::Path lensShape;
        lensShape.addEllipse (led);
        uv::light::glowLed (g, lensShape);
        g.setColour (c::uv);
        g.fillPath (lensShape);
        uv::type::draw (g, word, { x + lens + sp::space2, nameTop, wordW + 1.0f, readout.lineHeight }, labelFont, c::inkMuted);
    }

    /* The words. */
    uv::type::draw (g, uv::type::cased (label, state.description), { 0.0f, wordsTop, width, label.lineHeight },
                    uv::type::font (label), c::inkMuted);

    /* Notes, and the other readings. */
    const auto rowLabel = [&] (const juce::String& text, float top, float height) {
        uv::type::draw (g, uv::type::cased (label, text), { 0.0f, top, rowLabelW, height }, uv::type::font (label), c::inkMuted);
    };
    rowLabel ("Notes", notesTop, value.lineHeight);
    uv::type::draw (g, state.notes.isEmpty() ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) : state.notes,
                    { rowLabelW + sp::space3, notesTop, width - rowLabelW - sp::space3, value.lineHeight },
                    uv::type::font (value), state.notes.isEmpty() ? c::inkDim : c::ink);

    rowLabel ("Also", alsoTop, chipH);
    float x = rowLabelW + sp::space3;
    if (state.alternatives.isEmpty())
        uv::type::draw (g, juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")), { x, alsoTop, 16.0f, chipH },
                        uv::type::font (value), c::inkDim);
    for (const auto& alternative : state.alternatives)
    {
        if (x >= width)
            break;
        x += chip (g, alternative, x, alsoTop, juce::Colour(), c::line100, c::inkMuted) + sp::space2;
    }
}

} // namespace ni::ui

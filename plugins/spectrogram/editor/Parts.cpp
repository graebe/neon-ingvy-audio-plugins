// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

#include "Parts.h"

#include "UvTokens.h"
#include "UvType.h"
#include "WaveSource.h"

namespace ni::spectrogram
{

namespace c = uv::tok::colour;
namespace space = uv::tok::space;
constexpr float hair = uv::tok::size::hairline;

/* ------------------------------------------------------------------ Words */

namespace
{
const uv::tok::TextStyle& styleOf (Words::Voice v)
{
    return v == Words::Voice::quiet ? uv::tok::type::hint : uv::tok::type::label;
}

juce::Colour colourOf (Words::Voice v)
{
    /* Captions are labels, so ink-muted like every label: ink-dim is for
     * marks and disabled text, and fails contrast as running text. */
    return v == Words::Voice::warning ? c::amber : c::inkMuted;
}
} // namespace

Words::Words (Voice v, const juce::String& t) : voice (v), text (t)
{
    setInterceptsMouseClicks (false, false);
}

void Words::setText (const juce::String& t)
{
    if (t == text)
        return;
    text = t;
    repaint();
}

int Words::idealWidth() const
{
    const auto& style = styleOf (voice);
    return (int) std::ceil (uv::type::width (uv::type::font (style), uv::type::cased (style, text)));
}

void Words::paint (juce::Graphics& g)
{
    const auto& style = styleOf (voice);
    uv::type::draw (g, uv::type::cased (style, text), getLocalBounds().toFloat(), uv::type::font (style),
                    colourOf (voice), juce::Justification::centredLeft);
}

/* ---------------------------------------------------------------- Divider */

Divider::Divider()
{
    setInterceptsMouseClicks (false, false);
}

void Divider::paint (juce::Graphics& g)
{
    g.setColour (c::line100);
    g.fillRect (getLocalBounds().toFloat().reduced (0.0f, space::space1));
}

/* ------------------------------------------------------------------- Well */

Well::Well()
{
    setOpaque (true);
    /* The well is the wall; the picture inside it is part of it. */
    ni::ui::setWaveSource (*this);
    ni::ui::setWaveSource (spectrogram, false);
    addAndMakeVisible (spectrogram);
    setSize (pictureWidth + 2, pictureHeight + 2);
}

void Well::paint (juce::Graphics& g)
{
    g.fillAll (c::bg000);
    g.setColour (c::line200);
    g.drawRect (getLocalBounds().toFloat(), hair);
}

void Well::resized()
{
    spectrogram.setBounds (getLocalBounds().reduced ((int) hair));
}

/* --------------------------------------------------------- FrequencyScale */

FrequencyScale::FrequencyScale()
{
    setInterceptsMouseClicks (false, false);
}

void FrequencyScale::setMarks (std::vector<FreqMark> m)
{
    marks = std::move (m);
    repaint();
}

void FrequencyScale::paint (juce::Graphics& g)
{
    const auto& style = uv::tok::type::label;
    const auto font = uv::type::font (style);
    const float tick = space::space1;
    const float w = (float) getWidth();

    for (const auto& m : marks)
    {
        /* The picture starts a hairline into the well. */
        const float y = (float) overhang + hair + m.y;
        g.setColour (c::line200);
        g.fillRect (juce::Rectangle<float> (w - tick, std::floor (y - hair * 0.5f), tick, hair));
        uv::type::draw (g, uv::type::cased (style, m.label),
                        { 0.0f, y - style.lineHeight * 0.5f, w - tick - space::space1, style.lineHeight },
                        font, c::inkMuted, juce::Justification::centredRight);
    }
}

/* --------------------------------------------------------------- TimeAxis */

TimeAxis::TimeAxis()
{
    setInterceptsMouseClicks (false, false);
}

void TimeAxis::setMarks (std::vector<TimeMark> m, bool barView)
{
    marks = std::move (m);
    bars = barView;
    repaint();
}

void TimeAxis::paint (juce::Graphics& g)
{
    const auto& style = uv::tok::type::hint;
    const auto font = uv::type::font (style);
    const float tick = space::space1;
    /* The label's line box sits a gap under the tick. */
    const float labelTop = tick + space::space1;

    /* x is the picture's; the component starts `overhang` left of it. */
    g.addTransform (juce::AffineTransform::translation ((float) overhang, 0.0f));

    for (const auto& m : marks)
    {
        const auto tickColour = ! bars ? c::line200 : (m.beat ? c::line200 : c::inkDim);
        /* The tick is the mark's own 1 px column, hanging from its left edge
         * the way the web axis's centred, end- or start-anchored flex box
         * puts it: centred, at the right edge's last pixel, or at x. */
        float tx = m.x - hair * 0.5f;
        if (m.anchor == TimeMark::Anchor::end)
            tx = m.x - hair;
        else if (m.anchor == TimeMark::Anchor::start)
            tx = m.x;
        g.setColour (tickColour);
        g.fillRect (juce::Rectangle<float> (tx, 0.0f, hair, tick));

        if (m.label.isEmpty())
            continue;
        const float lw = uv::type::width (font, m.label);
        float lx = m.x - lw * 0.5f;
        auto just = juce::Justification::centredLeft;
        if (m.anchor == TimeMark::Anchor::end)
            lx = m.x - lw;
        else if (m.anchor == TimeMark::Anchor::start)
            lx = m.x;
        uv::type::draw (g, m.label, { lx, labelTop, lw + 1.0f, style.lineHeight }, font, c::inkMuted, just);
    }
}

/* ------------------------------------------------------- CrosshairReadout */

CrosshairReadout::CrosshairReadout()
{
    setInterceptsMouseClicks (false, false);
    freq = time = level = noReading();
}

void CrosshairReadout::setValues (const juce::String& f, const juce::String& key, const juce::String& t,
                                  const juce::String& l)
{
    if (f == freq && key == timeKey && t == time && l == level)
        return;
    freq = f;
    timeKey = key;
    time = t;
    level = l;
    repaint();
}

/*
 * ONE BASELINE. The keys are 11 px in a 14 px line and the values 13 px in a
 * 16 px line; aligned on their baselines, as the web row's flex does, the
 * taller value line sits at the top and each key's line drops by the
 * difference in where the two baselines fall in their lines.
 */
void CrosshairReadout::paint (juce::Graphics& g)
{
    const auto& keyStyle = uv::tok::type::label;
    const auto& valueStyle = uv::tok::type::value;
    const auto keyFont = uv::type::font (keyStyle);
    const auto valueFont = uv::type::font (valueStyle);

    const auto baselineIn = [] (const juce::Font& f, float lineHeight) {
        return (lineHeight - f.getHeight()) * 0.5f + f.getAscent();
    };
    const float keyDrop = baselineIn (valueFont, valueStyle.lineHeight) - baselineIn (keyFont, keyStyle.lineHeight);

    static constexpr float valueMin = 76.0f;
    const float gap = space::space2;
    float x = 0.0f;

    const juce::String keys[] { "freq", timeKey, "level" };
    const juce::String* values[] { &freq, &time, &level };
    for (int i = 0; i < 3; ++i)
    {
        /* The row's gap, and space-6 more before every key but the first. */
        if (i > 0)
            x += gap + space::space6;
        const auto key = uv::type::cased (keyStyle, keys[i]);
        const float kw = uv::type::width (keyFont, key);
        uv::type::draw (g, key, { x, keyDrop, kw + 1.0f, keyStyle.lineHeight }, keyFont, c::inkMuted);
        x += kw + gap;
        const float vw = juce::jmax (valueMin, uv::type::width (valueFont, *values[i]));
        uv::type::draw (g, *values[i], { x, 0.0f, vw + 1.0f, valueStyle.lineHeight }, valueFont, c::ink);
        x += vw;
    }
}

} // namespace ni::spectrogram

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's two wells. Plots.h says what each shows.
 */
#include "Plots.h"

#include "InfoLines.h"
#include "UvTokens.h"
#include "UvType.h"

#include <algorithm>
#include <cmath>

namespace ni::vz
{

namespace
{
namespace c = uv::tok::colour;

const char* const vowelLetters[] = { "A", "E", "I", "O", "U" };

/* Where `v` sits between `a` and `b` on a logarithmic scale, 0..1. */
float logFraction (double v, double a, double b)
{
    return (float) (std::log (v / a) / std::log (b / a));
}

/* A dot: a small circle, as a path. */
juce::Path dotPath (juce::Point<float> p, float radius)
{
    juce::Path d;
    d.addEllipse (p.x - radius, p.y - radius, 2.0f * radius, 2.0f * radius);
    return d;
}
} // namespace

/* ---------------------------------------------------------------- vowel -- */

VowelChart::VowelChart (Model& m) : model (m), along ((size_t) pathPoints)
{
    ni::ui::setInfo (*this, info::vowelChart);
    update();
}

juce::Point<float> VowelChart::at (const Formants& f) const
{
    const auto area = contentBounds();
    const float x = area.getX() + juce::jlimit (0.0f, 1.0f, logFraction (f[1], f2Left, f2Right)) * area.getWidth();
    const float y = area.getY() + juce::jlimit (0.0f, 1.0f, logFraction (f[0], f1Top, f1Bottom)) * area.getHeight();
    return { x, y };
}

void VowelChart::update()
{
    /* The path depends on Size (and so does the dot); Resonance moves no
     * formant, so a change of it redraws nothing. */
    const double v = model.parameter (param::vowel).convertFrom0to1 (model.parameter (param::vowel).getValue());
    std::vector<Formants> fresh ((size_t) pathPoints);
    for (int i = 0; i < pathPoints; ++i)
        fresh[(size_t) i] = model.formants (4.0 * i / (pathPoints - 1));
    const auto latest = model.voice();
    const bool voiceMoved = latest.serial != voice.serial;
    voice = latest;
    const auto caption = captionFor (model.parameter (param::vowel).getCurrentValueAsText());
    if (fresh == along && juce::exactlyEqual (v, shownVowel) && caption == getCaption() && ! voiceMoved)
        return;
    along = std::move (fresh);
    shownVowel = v;
    current = model.formants (v);
    setCaption (caption);
    rebuild();
    repaint();
}

/* The vowel, and where the voice came from: the recording's name, upper
 * case without its extension, cut to fit the well. */
juce::String VowelChart::captionFor (const juce::String& vowelText) const
{
    auto name = juce::File::createFileWithoutCheckingPath (voice.name).getFileNameWithoutExtension().toUpperCase();
    if (name.length() > 14)
        name = name.substring (0, 13) + juce::String (juce::CharPointer_UTF8 ("…"));
    const auto vowelPart = "VOWEL   " + vowelText.toUpperCase();
    switch (voice.status)
    {
        case Voice::Status::capturing:  return "VOWEL   CAPTURING " + name;
        case Voice::Status::captured:   return vowelPart + "   " + name;
        case Voice::Status::unreadable: return "VOWEL   CANNOT READ " + name;
        case Voice::Status::unsung:     return "VOWEL   NO VOWELS IN " + name;
        case Voice::Status::none:       break;
    }
    return vowelPart;
}

void VowelChart::setDropping (bool on)
{
    if (on == dropping)
        return;
    dropping = on;
    repaint();
}

void VowelChart::rebuild()
{
    path.clear();
    for (int i = 0; i < pathPoints; ++i)
    {
        const auto p = at (along[(size_t) i]);
        if (i == 0)
            path.startNewSubPath (p);
        else
            path.lineTo (p);
    }
    for (int k = 0; k < 5; ++k)
        letters[(size_t) k] = at (along[(size_t) (k * (pathPoints - 1) / 4)]);
    now = at (current);
}

void VowelChart::paintPlot (juce::Graphics& g)
{
    ni::ui::plot::ghost (g, path);

    /* The letters sit beside their vowel, away from the path's middle. */
    const auto font = uv::type::label();
    const auto area = contentBounds();
    g.setFont (font);
    for (int k = 0; k < 5; ++k)
    {
        const auto p = letters[(size_t) k];
        const float dx = p.x < area.getCentreX() ? -12.0f : 12.0f;
        const bool measured = voice.sung && voice.found[(size_t) k];
        g.setColour (measured ? c::ink : c::inkMuted);
        g.drawText (vowelLetters[k], juce::Rectangle<float> (p.x + dx - 8.0f, p.y - 8.0f, 16.0f, 16.0f),
                    juce::Justification::centred, false);
        g.setColour (c::line200);
        g.fillPath (dotPath (p, 2.0f));
    }

    const auto d = dotPath (now, 5.0f);
    ni::ui::plot::under (g, d);
    ni::ui::plot::curve (g, d);

    if (dropping)
    {
        g.setColour (c::uv);
        g.drawRect (getLocalBounds().toFloat().reduced (1.0f), 1.0f);
    }
}

/* ------------------------------------------------------------- envelope -- */

EnvelopePlot::EnvelopePlot (Model& m) : model (m)
{
    ni::ui::setInfo (*this, info::envelopePlot);
    setCaption ("ENVELOPE");
}

float EnvelopePlot::xOf (double hz) const
{
    return xAt (juce::jlimit (0.0f, 1.0f, logFraction (hz, lowestHz, highestHz)));
}

void EnvelopePlot::update()
{
    const auto& p = model.picture();
    const double v = model.parameter (param::vowel).convertFrom0to1 (model.parameter (param::vowel).getValue());
    const auto f = model.formants (v);
    const bool fresh = p.serial != shown.serial || p.count() != shown.count() || p.spectral != shown.spectral;
    if (! fresh && f == marks)
        return;
    if (fresh)
    {
        shown = p;
        const auto ms = shown.latencyMs();
        setCaption (shown.spectral ? "ENVELOPE   SPECTRAL   " + juce::String (juce::roundToInt (ms)) + " MS LATE"
                                   : juce::String ("ENVELOPE   TIME"));
    }
    marks = f;
    rebuild();
    repaint();
}

void EnvelopePlot::rebuild()
{
    inputLine.clear();
    outputLine.clear();
    const int n = shown.count();
    if (n < 2)
        return;
    const auto area = contentBounds();
    const auto yOf = [&area] (double db)
    { return area.getY() + (float) juce::jlimit (0.0, 1.0, db / floorDb) * area.getHeight(); };
    for (int i = 0; i < n; ++i)
    {
        const float x = xOf (shown.hz[(size_t) i]);
        const float yi = yOf (shown.input[(size_t) i]);
        const float yo = yOf (shown.output[(size_t) i]);
        if (i == 0)
        {
            inputLine.startNewSubPath (x, yi);
            outputLine.startNewSubPath (x, yo);
        }
        else
        {
            inputLine.lineTo (x, yi);
            outputLine.lineTo (x, yo);
        }
    }
}

void EnvelopePlot::paintPlot (juce::Graphics& g)
{
    const auto area = contentBounds();
    for (int k = 0; k < 2; ++k)
        ni::ui::plot::rule (g, xOf (marks[(size_t) k]), area.getY(), area.getBottom());
    ni::ui::plot::ghost (g, inputLine);
    ni::ui::plot::curve (g, outputLine);
}

} // namespace ni::vz

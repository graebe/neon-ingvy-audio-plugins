// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's two wells, side by side over the panels:
 *
 *   VowelChart    THE VOWEL SPACE, as phoneticians draw it: F2 across, high
 *                 on the left, and F1 down, so the chart is the mouth seen
 *                 from the side -- I front and closed at the top left, A open
 *                 at the bottom, U back at the top right. The five vowels are
 *                 letters on the path the Vowel knob walks (ghost); the dot
 *                 is where the vowel is now. Size slides the whole path along
 *                 its diagonal, so a turn of it is seen as well as heard; a
 *                 captured voice moves each vowel to where the singer sang
 *                 it, as far as Voice says. The caption repeats the vowel in
 *                 the engine's words ("VOWEL   E>I 30 %") and names the
 *                 recording a voice came from; the letters of vowels it
 *                 measured are drawn in ink, as bright as the capture was
 *                 sure of each, the rest muted. A recording
 *                 dragged over the window outlines the well: drop it there.
 *
 *   EnvelopePlot  THE SOUND'S COLOUR: the left channel's spectral envelope,
 *                 50 Hz to 10 kHz in equal steps of pitch, 0 to -48 dB --
 *                 coming in as a ghost line, going out as the curve in front,
 *                 the vowel's first two formants marked. At Vocal 0 the two
 *                 are one line; turned up, the front one takes the vowel's
 *                 shape. The caption names the engine drawing it, and a
 *                 Spectral engine's latency.
 *
 * Both draw what the model hands them; neither knows what a formant is
 * (Plot.h: "draw what the engine renders"). update() is the editor's frame:
 * it reads the model and repaints only when the picture changed. Message
 * thread.
 */
#pragma once

#include "Model.h"
#include "Plot.h"

#include <vector>

namespace ni::vz
{

class VowelChart final : public ni::ui::PlotWell
{
public:
    /* The chart's ranges, in Hz: F2 from left to right, F1 from top to
     * bottom -- room for every vowel at every Size. */
    static constexpr double f2Left = 3200.0, f2Right = 560.0;
    static constexpr double f1Top = 220.0, f1Bottom = 1100.0;
    /* Points along the path: twenty a step between vowels. */
    static constexpr int pathPoints = 81;

    explicit VowelChart (Model&);

    void update();

    /* While a recording is dragged over the window: the well says it will
     * take it. */
    void setDropping (bool);
    bool isDropping() const noexcept { return dropping; }

    /* Where the dot is, and the five letters, in the well's coordinates:
     * for the tests. */
    juce::Point<float> dot() const noexcept { return now; }
    juce::Point<float> letter (int vowel) const noexcept { return letters[(size_t) vowel]; }

    void resized() override { rebuild(); }

private:
    void paintPlot (juce::Graphics&) override;
    void rebuild();
    juce::Point<float> at (const Formants&) const;

    juce::String captionFor (const juce::String& vowelText) const;

    Model& model;
    Voice voice;
    bool dropping = false;
    double shownVowel = -1.0;
    std::vector<Formants> along;
    Formants current {};
    juce::Path path;
    std::array<juce::Point<float>, 5> letters {};
    juce::Point<float> now;
};

class EnvelopePlot final : public ni::ui::PlotWell
{
public:
    static constexpr double lowestHz = 50.0, highestHz = 10000.0;
    static constexpr double floorDb = -48.0;

    explicit EnvelopePlot (Model&);

    void update();

    /* Whether there is a picture yet: false before any audio. */
    bool hasPicture() const noexcept { return shown.count() > 0; }
    /* The x of a frequency, in the well's coordinates. */
    float xOf (double hz) const;

    void resized() override { rebuild(); }

private:
    void paintPlot (juce::Graphics&) override;
    void rebuild();

    Model& model;
    Picture shown;
    Formants marks {};
    juce::Path inputLine, outputLine;
};

} // namespace ni::vz

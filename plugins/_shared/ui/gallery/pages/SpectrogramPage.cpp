// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Spectrogram's picture in its states: the scrolling history, the bar
 * view's sweep with its playhead, and the clash over it -- amber as built,
 * and as ink hatching, the layouts' proposal SP4.
 *
 * The music is made up here, deterministically, in the shape the layouts draw
 * it: a low rumble, a kick on every beat with its click above it, hats high
 * up between them, and a lead line moving over four notes. Half-size pictures,
 * so the four fit the page; the editor's is 606 x 256.
 */
#include "DisplayPage.h"
#include "Gallery.h"

#include "Info.h"
#include "Spectrogram.h"

#include <cmath>
#include <vector>

namespace
{

using ni::ui::Spectrogram;

constexpr int cols = 303, bands = 128;

/* One column of the made-up music, `bands` bytes, band 0 lowest. */
void music (int t, std::vector<std::uint8_t>& out, std::vector<std::uint8_t>& clash)
{
    const int beat = 22;
    const int inBeat = t % beat;
    const int note = (t / (beat * 2)) % 4;
    static constexpr int lead[] { 70, 66, 62, 66 };

    for (int b = 0; b < bands; ++b)
    {
        float v = 0.0f;
        /* The rumble, low and steady, and a little air over the floor. */
        if (b < 14)
            v += 40.0f + 8.0f * std::sin ((float) (t + b) * 0.3f);
        v += juce::jmax (0.0f, 18.0f - (float) b * 0.4f);
        /* The kick: loud and low at the start of each beat, its click higher. */
        if (inBeat < 4 && b < 30)
            v += 190.0f - (float) inBeat * 35.0f - (float) b * 3.0f;
        if (inBeat < 2 && b >= 36 && b < 44)
            v += 110.0f;
        /* Hats, between the kicks. */
        if (inBeat >= 11 && inBeat < 13 && b > 96 && b < 120)
            v += 90.0f;
        /* The lead and its first two harmonics. */
        for (int h = 1; h <= 3; ++h)
        {
            const int centre = lead[note] + (h - 1) * 14;
            const int d = std::abs (b - centre);
            if (d < 2)
                v += (h == 1 ? 150.0f : 95.0f / (float) h) - (float) d * 30.0f;
        }
        out[(size_t) b] = (std::uint8_t) juce::jlimit (0, 255, juce::roundToInt (v));

        /* Where the kick meets the bass of another source, in the second half. */
        const bool clashes = t > cols / 2 && inBeat < 6 && b >= 4 && b < 26;
        clash[(size_t) b] = clashes ? (std::uint8_t) juce::jlimit (0, 255, 230 - inBeat * 25 - std::abs (b - 15) * 6) : 0;
    }
}

/* Fills a picture with `cols` columns of music, in batches as the plugin
 * sends them, each placed at its slot of a two-bar sweep. */
void fill (Spectrogram& s)
{
    std::vector<std::uint8_t> data ((size_t) bands * 3), clash ((size_t) bands * 3), one ((size_t) bands),
        oneClash ((size_t) bands);
    int slots[3] {};
    for (int t = 0; t < cols + 40; t += 3)
    {
        for (int i = 0; i < 3; ++i)
        {
            music (t + i, one, oneClash);
            std::copy (one.begin(), one.end(), data.begin() + i * bands);
            std::copy (oneClash.begin(), oneClash.end(), clash.begin() + i * bands);
            /* The sweep runs a little slower than the clock, so it gaps. */
            slots[i] = ((t + i) * 6 / 5) % cols;
        }
        s.push ({ data.data(), 3, bands, slots, clash.data() });
    }
}

struct SpectrogramPage final : public ni::ui::gallery::DisplayPage
{
    SpectrogramPage()
        : scroll (cols, bands), bars (cols, bands), amber (cols, bands), hatch (cols, bands)
    {
        constexpr int gap = 32, row = 22 + bands + 24;
        setSize (2 * cols + gap, 2 * row - 24);

        place (scroll, "history", 0, 0);
        place (bars, "bars, playhead", cols + gap, 0);
        place (amber, "clash, as built", 0, row);
        place (hatch, "clash, hatched (SP4)", cols + gap, row);

        bars.setView (Spectrogram::View::bars);
        amber.setClash (true);
        hatch.setClash (true);
        hatch.setClashStyle (Spectrogram::ClashStyle::hatch);
    }

    void place (Spectrogram& s, const char* name, int x, int y)
    {
        caption (name, { (float) x, (float) y, (float) cols, 14.0f });
        addAndMakeVisible (s);
        s.setBounds (x, y + 22, cols, bands);
        ni::ui::setInfo (s, juce::String::fromUTF8 ("Spectrogram \xe2\x80\x94 hover to read a point; clash marks where channels collide."));
        fill (s);
    }

    Spectrogram scroll, bars, amber, hatch;
};

} // namespace

NI_GALLERY_PAGE ("Display", "Spectrogram", [] { return std::make_unique<SpectrogramPage>(); });

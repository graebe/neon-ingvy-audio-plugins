// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Spectrogram's arithmetic: the frequency scale, the seconds and the bar
 * grid, a musical position's pixel and back, the readouts, and the channel
 * list the View and Compare pickers are built from. The web editor's
 * ui/test/columns.test.mjs, case for case where it tests the same rule, so
 * the native editor is held to the tables the web one was.
 */
#include "Channels.h"
#include "Scales.h"

#include <doctest.h>

#include <cmath>

using namespace ni::spectrogram;

namespace
{
const FreqMark* at (const std::vector<FreqMark>& marks, const char* label)
{
    for (const auto& m : marks)
        if (m.label == label)
            return &m;
    return nullptr;
}

/* `n` geometric band centres from lo to hi, as the analyzer sends them. */
std::vector<float> centres (int n, double lo, double hi)
{
    const double ratio = std::pow (hi / lo, 1.0 / n);
    std::vector<float> hz;
    for (int i = 0; i < n; ++i)
        hz.push_back ((float) (lo * std::pow (ratio, i) * std::sqrt (ratio)));
    return hz;
}

juce::String u (const char* utf8)
{
    return juce::String::fromUTF8 (utf8);
}
} // namespace

TEST_CASE ("spectrogram scales: a mark sits where its frequency sits, and the bass is at the bottom")
{
    const auto marks = freqMarks ({ 100.0f, 10000.0f }, 268.0f);
    REQUIRE (at (marks, "1k") != nullptr);
    CHECK (at (marks, "1k")->y == doctest::Approx (134.0));
    CHECK (at (marks, "100")->y == doctest::Approx (268.0));
    CHECK (at (marks, "10k")->y == doctest::Approx (0.0).epsilon (1e-6));
    /* A decade is a decade anywhere on the scale. */
    CHECK (at (marks, "100")->y - at (marks, "1k")->y == doctest::Approx (at (marks, "1k")->y - at (marks, "10k")->y));
}

TEST_CASE ("spectrogram scales: the scale reaches both ends of the full axis")
{
    const auto marks = freqMarks ({ 10.0f, 20000.0f }, 256.0f);
    juce::StringArray labels;
    for (const auto& m : marks)
        labels.add (m.label);
    CHECK (labels.joinIntoString (",") == "10,20,50,100,500,1k,5k,10k,20k");
    CHECK (at (marks, "10")->y == doctest::Approx (256.0));
    CHECK (at (marks, "20k")->y == doctest::Approx (0.0).epsilon (1e-4));
}

TEST_CASE ("spectrogram scales: the end marks survive the half band between a centre and an edge")
{
    const auto hz = centres (64, 10.0, 20000.0);
    const auto marks = freqMarks (hz, 256.0f);
    REQUIRE (at (marks, "10") != nullptr);
    REQUIRE (at (marks, "10k") != nullptr);
    CHECK (std::abs (at (marks, "10")->y - 256.0f) < 0.5f);
    const float decade = at (marks, "10")->y - at (marks, "100")->y;
    CHECK (at (marks, "100")->y - at (marks, "1k")->y == doctest::Approx (decade));
}

TEST_CASE ("spectrogram scales: marks outside the axis are left out, and every range has marks")
{
    const auto marks = freqMarks ({ 200.0f, 8000.0f }, 268.0f);
    REQUIRE (marks.size() == 3);
    CHECK (marks[0].label == "500");
    CHECK (marks[2].label == "5k");
    CHECK (freqMarks ({}, 268.0f).empty());
    CHECK (freqMarks ({ 1000.0f }, 268.0f).empty());

    CHECK (numRanges == 5);
    CHECK (juce::String (ranges[0].name) == "Full");
    for (const auto& r : ranges)
    {
        CAPTURE (r.name);
        CHECK (r.lo >= 10.0f);
        CHECK (r.hi <= 20000.0f);
        CHECK (r.hi >= r.lo * 2.0f);
        CHECK (freqMarks (centres (64, r.lo, r.hi), 256.0f).size() >= 2);
    }
}

TEST_CASE ("spectrogram scales: a range in Hz is its named zoom, or Full")
{
    CHECK (rangeIndex (10.0f, 20000.0f) == 0);
    CHECK (rangeIndex (40.0f, 800.0f) == 2);
    CHECK (rangeIndex (2000.2f, 19999.7f) == 4);
    CHECK (rangeIndex (30.0f, 300.0f) == 0);
}

TEST_CASE ("spectrogram scales: the Span is the seconds the picture holds, then the bar views")
{
    CHECK (spanOptions().joinIntoString ("|") == "13 s|1 bar|2 bars|4 bars|8 bars|16 bars");
    CHECK (spanOptions().size() == numBarCounts + 1);
}

TEST_CASE ("spectrogram scales: the facts are the span drawn, the floor, and in bars the tempo")
{
    CHECK (readTempo (120.0) == "120 BPM");
    CHECK (readTempo (127.5) == "127.5 BPM");
    CHECK (readTempo (99.96) == "100 BPM");

    CHECK (pictureFacts ({}, false, 120.0, true) == "waiting for the plugin");
    CHECK (pictureFacts ({ 440.0f }, true, 120.0, true) == "waiting for the plugin");

    /* The top is the analyzer's, clamped under Nyquist, not the range's. */
    const std::vector<float> hz { 10.0f, 100.0f, 19700.0f };
    CHECK (pictureFacts (hz, false, 120.0, false)
           == u ("10 Hz \xe2\x80\x93 19.7 kHz \xc2\xb7 floor \xe2\x88\x92" "96 dB"));
    CHECK (pictureFacts (hz, true, 120.0, true)
           == u ("10 Hz \xe2\x80\x93 19.7 kHz \xc2\xb7 floor \xe2\x88\x92" "96 dB \xc2\xb7 120 BPM"));
    CHECK (pictureFacts (hz, true, 120.0, false).endsWith ("120 BPM free"));
}

TEST_CASE ("spectrogram scales: the time axis is a tick a second, right edge to left")
{
    const auto marks = secondMarks (606, 47.0, 606.0f);
    REQUIRE (! marks.empty());
    CHECK (marks.front().label == "0 s");
    CHECK (marks.front().x == doctest::Approx (606.0));
    CHECK (marks.front().anchor == TimeMark::Anchor::end);
    CHECK (marks.back().label == u ("\xe2\x88\x92" "12 s"));
    CHECK (marks.back().x > 0.0f);
    for (size_t i = 1; i < marks.size(); ++i)
        CHECK (marks[i - 1].x - marks[i].x == doctest::Approx (47.0));
    CHECK (secondMarks (0, 47.0, 606.0f).empty());
    CHECK (secondMarks (606, 0.0, 606.0f).empty());
    CHECK (secondMarks (606, 47.0, 0.0f).empty());
    CHECK (secondMarks (606, 47.0, 606.0f, 0.0).empty());
}

TEST_CASE ("spectrogram scales: PPQ counts quarters, and a position always lands on the same pixel")
{
    CHECK (beatsPerBar (4, 4) == 4.0);
    CHECK (beatsPerBar (6, 8) == 3.0);
    CHECK (beatsPerBar (5, 4) == 5.0);
    CHECK (beatsPerBar (0, 4) == 4.0);

    const auto slot = [] (double ppq) { return slotForPpq (ppq, 4, 4, 4, 606); };
    CHECK (slot (0.0) == 0);
    CHECK (slot (8.0) == 303);
    for (double ppq : { 0.0, 1.0, 2.5, 7.0, 11.25, 15.5 })
    {
        CHECK (slot (ppq) == slot (ppq + 16.0));
        CHECK (slot (ppq) == slot (ppq + 160.0));
    }
    /* A count-in folds back into the window. */
    CHECK (slot (-0.5) > 303);
    CHECK (slot (-16.0) == slot (0.0));
    CHECK (slot (-0.5) == slot (15.5));
    CHECK (slotForPpq (std::nan (""), 4, 4, 4, 606) == 0);
    CHECK (slotForPpq (4.0, 0, 4, 4, 606) == 0);
}

TEST_CASE ("spectrogram scales: a pixel reads back as the bar and beat a DAW would show")
{
    CHECK (posForSlot (0, 4, 4, 4, 606).bar == 1);
    CHECK (posForSlot (0, 4, 4, 4, 606).beat == 1.0);
    CHECK (posForSlot (151, 4, 4, 4, 606).bar == 1);
    const auto q = posForSlot (152, 4, 4, 4, 606);
    CHECK (q.bar == 2);
    CHECK (std::abs (q.beat - 1.0) < 0.05);
    CHECK (posForSlot (303, 4, 4, 4, 606).bar == 3);
    for (double ppq : { 0.0, 1.0, 2.5, 7.0, 11.25, 15.5 })
    {
        const auto p = posForSlot (slotForPpq (ppq, 4, 4, 4, 606), 4, 4, 4, 606);
        CHECK (std::abs ((p.bar - 1) * 4 + (p.beat - 1) - ppq) < 0.05);
    }
    for (int s : { -5, 0, 300, 605, 99999 })
    {
        const auto p = posForSlot (s, 4, 4, 4, 606);
        CHECK (p.bar >= 1);
        CHECK (p.bar <= 4);
        CHECK (p.beat >= 1.0);
        CHECK (p.beat < 5.0);
    }
}

TEST_CASE ("spectrogram scales: the bar grid drops its beats when a bar gets too narrow")
{
    const auto four = barMarks (4, 4, 4, 606.0f);
    int bars = 0, beats = 0;
    for (const auto& m : four)
        (m.beat ? beats : bars)++;
    CHECK (bars == 4);
    CHECK (beats == 12);
    CHECK (four.front().label == "1");
    CHECK (four.front().x == 0.0f);

    int sixteenBeats = 0;
    for (const auto& m : barMarks (16, 4, 4, 606.0f))
        sixteenBeats += m.beat ? 1 : 0;
    CHECK (sixteenBeats == 0);

    int sixEightBeats = 0;
    for (const auto& m : barMarks (2, 6, 8, 606.0f))
        sixEightBeats += m.beat ? 1 : 0;
    CHECK (sixEightBeats == 4);

    CHECK (barMarks (0, 4, 4, 606.0f).empty());
    CHECK (barMarks (4, 4, 4, 0.0f).empty());
}

TEST_CASE ("spectrogram scales: a level byte reads back as the decibel the engine encoded")
{
    CHECK (dbForLevel (255) == 0.0);
    CHECK (std::isinf (dbForLevel (0)));
    CHECK (std::isinf (dbForLevel (-3)));
    CHECK (std::abs (dbForLevel (128) - -48.0) < 0.2);
    for (int v = 2; v < 256; ++v)
        REQUIRE (dbForLevel (v) > dbForLevel (v - 1));
}

TEST_CASE ("spectrogram scales: the readouts say what the web editor said")
{
    CHECK (asHz (62.4) == "62 Hz");
    CHECK (asHz (999.4) == "999 Hz");
    CHECK (asHz (1234.0) == "1.2 kHz");
    CHECK (readFrequency ({ 20.6f, 41.2f }, 1) == "41 Hz");
    CHECK (readFrequency ({ 20.6f }, 3) == noReading());
    CHECK (noReading() == u ("\xe2\x80\x94"));

    CHECK (readAge (0) == "now");
    CHECK (readAge (47) == u ("\xe2\x88\x92" "1.00 s"));
    CHECK (readAge (113) == u ("\xe2\x88\x92" "2.40 s"));

    CHECK (readLevel (0) == u ("< \xe2\x88\x92" "96 dB"));
    CHECK (readLevel (255) == u ("\xe2\x88\x92" "0.0 dB"));
    CHECK (readLevel (208) == u ("\xe2\x88\x92" "17.7 dB"));

    /* "8:4.7": bar, colon, beat. */
    CHECK (readPosition (0, 4, 4, 4) == "1:1.0");
    CHECK (readPosition (303, 4, 4, 4) == "3:1.0");
}

/* -------------------------------------------------------------- channels */

namespace
{
std::vector<Source> buses()
{
    return {
        { 1, true, 48000, "bass" },
        { 2, false, 48000, "muted" },
        { 3, true, 48000, "" },
        { 5, true, 96000, "pad" },
    };
}
} // namespace

TEST_CASE ("spectrogram channels: this track, then every live bus in slot order, by name")
{
    const auto names = channelNames (buses());
    CHECK (names.joinIntoString ("|") == "input|bass|Bus 3|pad");
    CHECK (liveBuses (buses()).size() == 3);
    CHECK (sourceName ({ 3, true, 0, "" }) == "Bus 3");
    CHECK (sourceName ({ 3, true, 0, "Kick" }) == "Kick");
    CHECK (channelNames ({}).joinIntoString ("|") == "input");
}

TEST_CASE ("spectrogram channels: a bus at another rate is shown and refused, with its rate")
{
    Transport t;
    t.sampleRate = 48000;
    const auto options = viewOptions (buses(), referenceRate (t, buses()));
    REQUIRE (options.size() == 4);
    CHECK (options[0].name == "input");
    CHECK_FALSE (options[0].disabled);
    CHECK (options[1].hint.isEmpty());
    CHECK_FALSE (options[1].disabled);
    CHECK (options[3].name == "pad");
    CHECK (options[3].disabled);
    CHECK (options[3].hint == "96k");

    /* A rate not yet known is the first bus's, never 0, which would refuse
     * every bus. */
    CHECK (referenceRate (Transport {}, buses()) == 48000);
    CHECK (referenceRate (Transport {}, {}) == 0);
}

TEST_CASE ("spectrogram channels: the face names the view while it fits, then counts")
{
    const auto names = channelNames (buses());
    CHECK (viewSummary ({ 0 }, names) == "input");
    CHECK (viewSummary ({ 0, 1 }, names) == "input, bass");
    CHECK (viewSummary ({ 0, 1, 2 }, names) == "input +2");
    CHECK (viewSummary ({}, names) == "nothing");
    CHECK (viewSummary ({ 9 }, names) == "nothing");
}

TEST_CASE ("spectrogram channels: the buses to open are derived from the view and the comparison")
{
    /* The view's buses alone while the clash is off. */
    CHECK (listenSlots ({ 0, 1 }, 0, 2, false, buses()) == std::vector<int> { 1 });
    /* Both ends of the comparison too while it is on, the own channel never. */
    CHECK (listenSlots ({ 0, 1 }, 0, 2, true, buses()) == std::vector<int> { 1, 3 });
    CHECK (listenSlots ({ 0 }, 3, 2, true, buses()) == std::vector<int> { 3, 5 });
    /* At most three, the lowest channels first. */
    CHECK (listenSlots ({ 1, 2, 3 }, 0, 0, false, buses()) == std::vector<int> { 1, 3, 5 });
    /* A channel past the list opens nothing. */
    CHECK (listenSlots ({ 0, 7 }, 0, 1, false, buses()).empty());
}

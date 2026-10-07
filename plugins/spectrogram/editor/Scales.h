// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Where the picture's numbers go, and what they read: the frequency scale,
 * the seconds and the bar grid under the picture, the column a musical
 * position lands on, and the crosshair's three readouts. The web editor's
 * lib/columns.js, natively, rule for rule.
 *
 * PLAIN FUNCTIONS OVER NUMBERS, apart from any component, because these are
 * the only things in the editor that can be WRONG rather than merely ugly: a
 * label a band off, a column on the wrong beat, a dB read through the wrong
 * mapping. tests/ui/spectrogram_scales.cpp holds them to the web editor's
 * tables.
 *
 * NOTHING HERE DECIDES ANYTHING ABOUT THE ANALYSIS. The band count, the log
 * mapping and the dB floor are the engine's; the scale is placed from the
 * centres it sends, and the level is read back through the inverse of the
 * mapping that wrote it.
 */
#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace ni::spectrogram
{

/* ---------------------------------------------------------------- picture */

/* One column per pixel and one band per pixel: 606 columns at about 47 a
 * second is thirteen seconds, and the 256 bands fill 256 pixels. */
inline constexpr int pictureWidth = 606;
inline constexpr int pictureHeight = 256;

/* About 47 columns a second, held constant across sample rates by the
 * engine's hop rule -- so this one number turns the width into time. */
inline constexpr double columnsPerSecond = 47.0;

/* The engine's byte mapping: round((dB - floor) / (ceil - floor) * 255). */
inline constexpr double dbFloor = -96.0;
inline constexpr double dbCeil = 0.0;

/* What counts as a clash: both sources above -60 dB, and within 12 dB of each
 * other. The floor alone is not enough -- a product of two spectra is a sum
 * in dB, so 0 against -60 scores what -30 against -30 scores. */
inline constexpr float clashFloorDb = -60.0f;
inline constexpr float clashBalanceDb = 12.0f;

/* ------------------------------------------------------------------- zoom */

/* The zoom as named bands, overlapping on purpose: a kick's fundamental and
 * its click belong to different views, and each should be in more than one. */
struct Range
{
    const char* name;
    float lo, hi;
};

inline constexpr Range ranges[] {
    { "Full", 10.0f, 20000.0f },
    { "Sub", 10.0f, 200.0f },
    { "Bass", 40.0f, 800.0f },
    { "Mid", 200.0f, 4000.0f },
    { "High", 2000.0f, 20000.0f },
};
inline constexpr int numRanges = (int) (sizeof (ranges) / sizeof (ranges[0]));

/* The named zoom a range in Hz is, or 0 (Full) for one the list lacks. */
int rangeIndex (float lo, float hi);

/* The bar view's widths, in bars. */
inline constexpr int barCounts[] { 1, 2, 4, 8, 16 };
inline constexpr int numBarCounts = (int) (sizeof (barCounts) / sizeof (barCounts[0]));
/* The width the bar view keeps until another is chosen: 4 bars, as the web
 * editor's bars switch opened on. */
inline constexpr int defaultBarCount = 2;

/*
 * THE SPAN, one setting for what the x-axis is and how much of it (proposal
 * SP2): the seconds the picture holds, then each bar view's width --
 * "13 s", "1 bar", "2 bars" ... "16 bars". Option 0 is the seconds; option
 * n is barCounts[n - 1].
 */
juce::StringArray spanOptions();

/* -------------------------------------------------------------- the facts */

/* "120 BPM", "127.5 BPM": the host's tempo, to a tenth. */
juce::String readTempo (double bpm);

/*
 * The picture's facts, which the web editor's hint bar printed and 1.1.0's
 * Hint no longer holds: the span the analyzer really drew, band centre to
 * band centre (its top clamped below Nyquist), and the floor; in the bar view
 * the tempo the sweep is placed by, and `free` while no playhead drives it.
 * "waiting for the plugin" before the first axis.
 */
juce::String pictureFacts (const std::vector<float>& hz, bool barView, double bpm, bool running);

/* ------------------------------------------------------------------ marks */

/* A label on the frequency scale: its text and its height in the picture,
 * 0 at the top. */
struct FreqMark
{
    juce::String label;
    float y = 0.0f;
};

/*
 * The decades and their halves the axis covers (10 ... 20k), placed by the
 * analyzer's centres. With three or more centres the axis is widened by half
 * a band each way: the picture spans band EDGES and the list holds CENTRES,
 * and the end marks live in that half band. A mark within 1e-6 of an end is
 * at that end.
 */
std::vector<FreqMark> freqMarks (const std::vector<float>& hz, float height);

/* A tick under the picture: its label (empty for a beat), where it is, how
 * the label hangs from it, and whether it is a beat inside a bar. */
struct TimeMark
{
    enum class Anchor { start, mid, end };

    juce::String label;
    float x = 0.0f;
    Anchor anchor = Anchor::mid;
    bool beat = false;
};

/* A tick every `stepS` seconds from "0 s" at the right edge leftwards, the end
 * labels anchored inside the picture. */
std::vector<TimeMark> secondMarks (int columns, double colsPerSecond, float width, double stepS = 1.0);

/* One numbered tick per bar, and the beats inside it while a beat is at
 * least 14 px wide. */
std::vector<TimeMark> barMarks (int bars, int numerator, int denominator, float width);

/* ------------------------------------------------------------------- bars */

/* Quarter notes in a bar: the host counts PPQ in quarters, so 6/8 is 3. */
double beatsPerBar (int numerator, int denominator);

/* The pixel column a musical position belongs to in a window of `bars`,
 * 0 at the left -- wrapped for a negative position (a count-in), clamped
 * at the top. */
int slotForPpq (double ppq, int bars, int numerator, int denominator, int columns);

/* The inverse, for the crosshair: one-based, as a DAW's transport counts. */
struct BarPosition
{
    int bar = 1;
    double beat = 1.0;
};
BarPosition posForSlot (int slot, int bars, int numerator, int denominator, int columns);

/* --------------------------------------------------------------- readouts */

/* A level byte as dBFS; byte 0 is "at or below the floor": -infinity. */
double dbForLevel (int level);

/* "200 Hz", "1.2 kHz": the unit follows the number. */
juce::String asHz (double hz);

/* The crosshair's three, as the web editor writes them. */
juce::String readFrequency (const std::vector<float>& hz, int band);
juce::String readAge (int ageColumns);
juce::String readPosition (int slot, int bars, int numerator, int denominator);
juce::String readLevel (int level);

/* What a readout says with the pointer away: a dash, not a stale number. */
juce::String noReading();

} // namespace ni::spectrogram

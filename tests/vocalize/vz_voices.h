// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A SINGER FOR THE TESTS: recordings of a voice, made the way a voice is
 * made, written where a capture can read them.
 *
 *   sing()       vowels sung by a voice that is the textbook's (model.rs)
 *                times `scale` -- a glottal pulse train, tilted -6 dB an
 *                octave, through the vowel's five resonators, a breath of
 *                silence after each: vz-core's voice.rs tests' own singer
 *   writeWav()   a mono recording as a 32-bit float WAV, so what a capture
 *                reads back is exactly what was sung
 *   Scratch      a directory of its own under the system's temporary one,
 *                gone with the test
 *
 * The test's stand-in for a person, not the engine's: what the engine
 * measures from it is the engine's (vz_voice_capture).
 */
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <vector>

namespace ni::vz::test
{

/* The textbook voice (model.rs): five vowels' formants, A E I O U, F1 first,
 * and their bandwidths at Resonance 50 %. */
inline constexpr double textbook[5][5] = {
    { 768.0, 1333.0, 2522.0, 3500.0, 4500.0 },
    { 476.0, 2089.0, 2691.0, 3500.0, 4500.0 },
    { 342.0, 2322.0, 3000.0, 3500.0, 4500.0 },
    { 497.0, 910.0, 2459.0, 3500.0, 4500.0 },
    { 378.0, 997.0, 2343.0, 3500.0, 4500.0 },
};
inline constexpr double textbookBandwidths[5] = { 60.0, 90.0, 120.0, 200.0, 250.0 };

/* `vowels` (0..4) sung in turn for `seconds` each at `pitch`, by a voice
 * `scale` times the textbook's, at `fs`; peaking at 0.8. */
inline std::vector<float> sing (std::initializer_list<int> vowels, double scale, double pitch, double seconds, double fs)
{
    std::vector<double> out;
    for (const int v : vowels)
    {
        /* Klatt's resonator, unity gain at DC: model.rs's Pole. */
        struct Pole
        {
            double a = 1.0, b = 0.0, c = 0.0, y1 = 0.0, y2 = 0.0;
            double run (double x)
            {
                const double y = a * x + b * y1 + c * y2;
                y2 = y1;
                y1 = y;
                return y;
            }
        };
        Pole poles[5];
        for (int k = 0; k < 5; ++k)
        {
            const double f = textbook[v][k] * scale;
            if (f >= 0.45 * fs)
                continue;
            const double rho = std::exp (-juce::MathConstants<double>::pi * textbookBandwidths[k] / fs);
            poles[k].b = 2.0 * rho * std::cos (juce::MathConstants<double>::twoPi * f / fs);
            poles[k].c = -rho * rho;
            poles[k].a = 1.0 - poles[k].b - poles[k].c;
        }
        const double period = fs / pitch;
        double tilt = 0.0;
        const auto n = (int) (seconds * fs);
        for (int i = 0; i < n; ++i)
        {
            const double pulse = std::fmod ((double) i, period) < 1.0 ? 1.0 : 0.0;
            tilt = 0.97 * tilt + pulse;
            double x = tilt;
            for (auto& p : poles)
                x = p.run (x);
            out.push_back (x * 0.05);
        }
        out.insert (out.end(), (size_t) (0.1 * fs), 0.0);
    }
    double peak = 0.0;
    for (const auto x : out)
        peak = std::max (peak, std::abs (x));
    std::vector<float> scaled;
    scaled.reserve (out.size());
    for (const auto x : out)
        scaled.push_back ((float) (x / peak * 0.8));
    return scaled;
}

/* `samples` as a mono 32-bit float WAV at `fs`. */
inline bool writeWav (const juce::File& file, const std::vector<float>& samples, double fs)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (! static_cast<juce::FileOutputStream&> (*stream).openedOk())
        return false;
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions()
                             .withSampleRate (fs)
                             .withNumChannels (1)
                             .withBitsPerSample (32)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return false;
    const float* channels[] = { samples.data() };
    return writer->writeFromFloatArrays (channels, 1, (int) samples.size());
}

/* A directory of the test's own, removed with it. */
struct Scratch
{
    juce::File dir;

    explicit Scratch (const juce::String& name)
        : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getNonexistentChildFile (name, {}, false))
    {
        dir.createDirectory();
    }

    ~Scratch() { dir.deleteRecursively(); }

    juce::File operator/ (const juce::String& file) const { return dir.getChildFile (file); }

    JUCE_DECLARE_NON_COPYABLE (Scratch)
};

} // namespace ni::vz::test

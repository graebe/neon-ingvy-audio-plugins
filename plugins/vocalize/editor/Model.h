// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What NI Vocalize's editor needs from its plugin, and all it may ask.
 *
 * EVERY CONTROL IS A HOST PARAMETER, the engine's nine in its table's order
 * (vz_param_*): parameter(param::vowel) is Vowel. Their text is the engine's
 * ("80 %", "E>I 30 %", "Spectral"), and the editor holds no unit, precision
 * or range of its own. The window sends back parameter edits, and one thing
 * more: a recording to capture a voice from, or the word to forget it.
 *
 * WHAT IT SHOWS, ALL OF IT THE ENGINE'S:
 *
 *   formants()   where a vowel's formants lie, from the engine's own model
 *                (vz_formants) in the voice it sings with -- the captured one
 *                as far as Voice says -- at Size and Resonance as the host
 *                holds them NOW, so the vowel chart moves the moment a knob
 *                does
 *   voice()      whether a voice is captured, from what, and which vowels
 *                were measured
 *   picture()    the left channel's spectral envelope coming in and going
 *                out, as the audio thread published it (vz_shell_read)
 *
 * MESSAGE THREAD, EVERY CALL (EditorModel.h). The processor implements this
 * (Vocalize.h); tests/ui/vocalize_fakes.h implements it with nothing behind
 * it.
 */
#pragma once

#include "EditorModel.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ni::vz
{

/* The host parameters' indices: the engine table's order, so the host index
 * is the engine index. */
namespace param
{
enum : int
{
    mode = 0,   // Time or Spectral; not automated
    vocal,      // 0..100 %, how far the sound has become the vowel
    vowel,      // 0..4, A E I O U and the way between
    size,       // -100..+100 %, the voice bigger and lower, or smaller
    resonance,  // 0..100 %, the formants broad or sharp
    excite,     // 0..100 %, harmonics for pure sources
    mix,        // 0..100 %
    output,     // -24..+24 dB
    voice,      // 0..100 %, the textbook voice to the captured one
    count
};
} // namespace param

/* F1..F5, in Hz. */
using Formants = std::array<double, 5>;

/*
 * THE PICTURE: at each frequency in `hz` (lowest first, equally spaced in
 * pitch), the envelope coming in and going out, in dB, each with its peak at
 * 0. Empty before the plugin has processed anything. `serial` changes
 * whenever new audio arrived; `spectral` and `latency` say which engine drew
 * it.
 */
struct Picture
{
    std::vector<double> hz, input, output;
    double sampleRate = 0.0;
    bool spectral = false;
    int latency = 0;
    std::uint32_t serial = 0;

    int count() const noexcept { return (int) hz.size(); }
    /* The latency in ms, or 0 before the rate is known. */
    double latencyMs() const noexcept { return sampleRate > 0.0 ? 1000.0 * latency / sampleRate : 0.0; }
};

/*
 * THE CAPTURED VOICE. `status` says where the latest capture is: none yet
 * (or forgotten), one under way, one done, or one that found nothing it could
 * use -- `name` is that recording's file name in each case but none.
 *
 * WHAT IS SUNG is said apart from it, because a capture that fails keeps the
 * voice before it: `sung` is whether a captured voice is sung, `sungName` the
 * recording it came from, and `found` which of its vowels (A E I O U) were
 * measured rather than filled in from the textbook. `serial` changes
 * whenever any of it does.
 */
struct Voice
{
    enum class Status
    {
        none,
        capturing,
        captured,
        unreadable,   // not a recording this plugin can read
        unsung        // read, and nothing in it was a sung vowel
    };
    Status status = Status::none;
    juce::String name;
    bool sung = false;
    juce::String sungName;
    std::array<bool, 5> found {};
    std::uint32_t serial = 0;
};

class Model : public ni::ui::EditorModel
{
public:
    /* Whether parameter `index` runs both ways about zero, so its knob draws
     * its arc from 12 o'clock: the engine's table says. */
    virtual bool bipolar (int index) const = 0;

    /* The formants at `vowel` (0..4) of the voice sung with, Size and
     * Resonance as they are now. */
    virtual Formants formants (double vowel) = 0;

    /* The captured voice as it stands. */
    virtual Voice voice() const = 0;

    /* Capture a voice from `recording`, in the background: voice() says
     * capturing at once, and how it went when it has. A capture under way
     * makes this do nothing. */
    virtual void capture (const juce::File& recording) = 0;

    /* Forget the captured voice: the textbook's is sung again. */
    virtual void forgetVoice() = 0;

    /* The files a voice can be captured from, as a wildcard ("*.wav;*.aif"). */
    virtual juce::String recordingWildcard() const = 0;

    /* The latest picture; valid until the next call. */
    virtual const Picture& picture() = 0;
};

} // namespace ni::vz

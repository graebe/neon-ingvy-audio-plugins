// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What NI Bit-Crusher's editor needs from its plugin, and all it may ask.
 *
 * EVERY CONTROL IS A HOST PARAMETER, the engine's eighteen in its table's
 * order (bc_param_*): parameter(param::bits) is Bits. Their text is the
 * engine's ("7.5 bits", "12.0 kHz", "+3.0 dB"), and the editor holds no unit,
 * precision or range of its own. The window sends nothing back but parameter
 * edits. Refresh is the window's own: saved with the set, not automated, and
 * nothing the engine does depends on it.
 *
 * THE PICTURES ARE THE ENGINE'S:
 *
 *   steps()   the staircase, as the engine renders it from the parameters
 *             NOW (bc_transfer_render), over the whole range -- so a turned
 *             knob moves the picture at once, and the picture is the
 *             engine's grid, never a second model of it drawn here
 *   filterResponse(), filterGain()  the filter's curve, and one point of it
 *             (bc_filter_response, bc_filter_gain)
 *   scope()   one look at the left channel, as the audio thread published it
 *             (bc_shell_read): the last few milliseconds dry and wet,
 *             starting on an upward zero crossing when there is one; the same
 *             samples in the quantiser; and the hold's own window
 *   refreshPeriodMs()  how long the pictures keep one look (Refresh)
 *
 * MESSAGE THREAD, EVERY CALL (EditorModel.h). The processor implements this
 * (BitCrusher.h); tests/ui/bit-crusher_fakes.h implements it with nothing
 * behind it.
 */
#pragma once

#include "EditorModel.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ni::bc
{

/* The host parameters' indices: the engine table's order, so the host index
 * is the engine index. */
namespace param
{
enum : int
{
    drive = 0,   // -24..+24 dB, before the crusher
    bits,        // 1..16, fractional
    shape,       // 0..100 %, even to µ-law steps
    midRise,     // switch: no level at zero
    dither,      // 0..100 %
    antiAlias,   // switch: ADAA on the steps
    rate,        // 50 Hz..96 kHz, equal ratios
    jitter,      // 0..100 %
    preFilter,   // switch
    postFilter,  // switch
    postOctave,  // -3..+3 octaves from Rate/2
    mix,         // 0..100 %
    output,      // -24..+24 dB
    refresh,     // Live, 10/s, 4/s, 1/s: the window's, not automated
    filter,      // switch: the resonant low-pass after the crusher, off by default
    cutoff,      // 20 Hz..20 kHz, equal ratios
    peak,        // 0..+24 dB at the cutoff
    slope,       // 12, 24, 48 dB per octave
    count
};
} // namespace param

/*
 * THE SCOPE: `dry` and `wet` the same length, -1..1 at full scale, oldest
 * first, at `sampleRate`. Empty before the plugin has processed anything.
 * `serial` changes whenever new audio arrived; `triggered` is false when the
 * window could not start on an upward zero crossing (silence, DC, a very low
 * tone), so the picture may crawl.
 *
 * THE SAME LOOK IN THE TWO DISCRETIZATIONS. `ampIn` and `ampOut` are the same
 * samples as `dry` in the quantiser: its input on the staircase's input axis
 * (after Drive) and the level it became, Dither in it. `holdIn` and
 * `holdTook` are the hold's own window, oldest first: the quantiser's input
 * and whether the hold took a level in each sample, `holdAt` past its start
 * (0..1: 0 on the host's grid, the exact instant with Anti-Alias on) -- the
 * latest `holdTakes` takes, 17 when the window had room for them.
 *
 * WHICH SETTINGS MADE IT. `settings` are the engine's values the look was
 * made with, by parameter index, and `settled` how many samples they had held
 * when it was taken: past `settledWindow`, the whole look is theirs. A
 * picture of the staircase NOW shows a look's samples only when the look is
 * the staircase's (Editor::current).
 */
struct Scope
{
    std::vector<float> dry, wet;
    std::vector<float> ampIn, ampOut;
    std::vector<float> holdIn;
    std::vector<std::uint8_t> holdTook;
    std::vector<float> holdAt;
    int holdTakes = 0;
    std::uint32_t settled = 0;
    std::array<double, param::count> settings {};

    /* Samples a look's settings must have held for all of it to be theirs:
     * the ring the windows are cut from. */
    static constexpr std::uint32_t settledWindow = 2048;
    double sampleRate = 0.0;
    bool triggered = false;
    std::uint32_t serial = 0;

    int count() const noexcept { return (int) dry.size(); }
    /* The window's length in ms, or 0 before the rate is known. */
    double ms() const noexcept { return sampleRate > 0.0 ? 1000.0 * (double) count() / sampleRate : 0.0; }
};

class Model : public ni::ui::EditorModel
{
public:
    /* Whether parameter `index` runs both ways about zero, so its knob draws
     * its arc from 12 o'clock: the engine's table says. */
    virtual bool bipolar (int index) const = 0;

    /* The staircase: `count` outputs for inputs evenly spaced over the whole
     * range, -1 to 1, both ends included -- on the quantiser's own input,
     * after Drive, so it always runs along the diagonal: Bits, Shape and
     * Mid-Rise as the host parameters hold them now. Drive is in the look's
     * samples instead (Scope::ampIn), where it moves the signal up the
     * staircase. Outputs are in full-scale units, as the inputs are. */
    virtual void steps (float* out, int count) = 0;

    /* The latest scope; valid until the next call. */
    virtual const Scope& scope() = 0;

    /* How long the pictures keep one look before taking the next, in ms, as
     * Refresh says now: 0 for Live, every frame. */
    virtual double refreshPeriodMs() const = 0;

    /* Parameter `index` as the engine is given it now -- on its resolution,
     * the number a look's `settings` hold when it was made with it. */
    virtual double setting (int index) const = 0;

    /* How many levels the staircase has now: 2^Bits + 1 mid-tread, 2^Bits
     * with Mid-Rise (bc_level_count). */
    virtual int levelCount() const = 0;

    /* The filter's response NOW, as the engine's sections have it
     * (bc_filter_response): `count` gains in dB at frequencies evenly spaced
     * in log from `loHz` to `hiHz`, at Cutoff, Peak and Slope as the host
     * parameters hold them, whether the Filter is on or not. */
    virtual void filterResponse (float* outDb, int count, double loHz, double hiHz) const = 0;

    /* One point of that response: the gain in dB at `hz` (bc_filter_gain),
     * where the handle sits on the curve. */
    virtual float filterGain (double hz) const = 0;
};

} // namespace ni::bc

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What NI Bit-Crusher's editor needs from its plugin, and all it may ask.
 *
 * EVERY CONTROL IS A HOST PARAMETER, the engine's fourteen in its table's
 * order (bc_param_*): parameter(param::bits) is Bits. Their text is the
 * engine's ("7.5 bits", "12.0 kHz", "+3.0 dB"), and the editor holds no unit,
 * precision or range of its own. The window sends nothing back but parameter
 * edits. Refresh is the window's own: saved with the set, not automated, and
 * nothing the engine does depends on it.
 *
 * THE PICTURES ARE THE ENGINE'S:
 *
 *   steps()   the staircase, as the engine renders it from the parameters
 *             NOW (bc_transfer_render), over the window the engine picks for
 *             it (bc_transfer_span) -- so a turned knob moves the picture at
 *             once, and the picture is the engine's grid, never a second
 *             model of it drawn here
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
 * (Drive taken back out) and the level it became, Dither in it. `holdIn` and
 * `holdTook` are the hold's own window, oldest first: the quantiser's input
 * and whether the hold took each sample -- the latest `holdTakes` takes, 17
 * when the window had room for them (16 periods).
 */
struct Scope
{
    std::vector<float> dry, wet;
    std::vector<float> ampIn, ampOut;
    std::vector<float> holdIn;
    std::vector<std::uint8_t> holdTook;
    int holdTakes = 0;
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

    /* The staircase: `count` outputs for inputs evenly spaced from -span to
     * span, both ends included -- Drive, Bits, Shape and Mid-Rise as the host
     * parameters hold them now. Returns the span, the engine's
     * (bc_transfer_span): the window around silence, a power of two from 1
     * down, that shows at most 16 of the steps, so each is wide enough to see.
     * Outputs are in full-scale units, as the inputs are. */
    virtual double steps (float* out, int count) = 0;

    /* The latest scope; valid until the next call. */
    virtual const Scope& scope() = 0;

    /* How long the pictures keep one look before taking the next, in ms, as
     * Refresh says now: 0 for Live, every frame. */
    virtual double refreshPeriodMs() const = 0;
};

} // namespace ni::bc

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the Side-Chain's editor needs from its plugin, and all it may ask.
 *
 * The web editor was fed through message tags (ui/src/lib/msg.js): the
 * engine's `ui`, `params` and `stage_ms` readouts as colon-separated text
 * every idle tick, the key bus, and the capture as six bytes a column. This is the same traffic as C++: snapshots the editor
 * reads when it wants them -- no tags, no text where the data is a number, no
 * format to parse. The Side-Chain sends nothing back but parameter edits:
 * everything it holds is a host parameter, the shape's handles included.
 *
 * MESSAGE THREAD, EVERY CALL (EditorModel.h). A getter returns the latest
 * snapshot the engine published, already copied out of its triple buffer; a
 * reference it returns stays valid until the same getter is called again. The
 * processor implements this (EngineModel.h); tests/ui/side-chain_fakes.h
 * implements it with nothing behind it.
 *
 * THE HOST PARAMETERS ARE THE PLUGIN'S FIFTEEN, in the iPlug2 index order
 * that is their VST3 ID (Params.h, and
 * tests/fixtures/iplug2/NISideChain/parameters.json): parameter(param::delay)
 * is Delay. Bypass is the host's and is not among them. Their text is the
 * plugin's ("35.0 %", "-inf dB", "C1", "Omni"), and the editor holds no unit,
 * precision or label of its own.
 *
 * THE SHAPE IS THE ENGINE'S. The web editor drew the duck from a JavaScript
 * port of shape.rs, pinned to a table the engine generated. Ultraviolet
 * 1.1.0's PlotWell says it outright: draw what the engine renders, never a
 * second model of the DSP rebuilt from the parameters. So the curve and the
 * positions of its four handles come from here, computed by the engine from
 * the host parameters as they are NOW -- which also puts a dragged handle
 * under the pointer at once, where the web editor had to fake a local copy
 * of the shape for the length of a drag.
 */
#pragma once

#include "EditorModel.h"

#include <cstdint>
#include <vector>

namespace ni::sc
{

/* The host parameters' indices: sc_param_t's order, so the host index is the
 * engine index. */
namespace param
{
enum : int
{
    source = 0,     // Cycle / MIDI / Sidechain
    rate,           // a choice of 12, the engine's labels ("1/4", "1/8T")
    timeMode,       // Time: "ms" / "% of cycle" -- a reading, not a sound
    delay,          // -100..+100 % of the cycle: bipolar
    attack,         // 0..200 % of the cycle
    hold,           // 0..200 %
    release,        // 0..200 %
    depth,          // 0..100 %
    curve,          // Linear / Exponential / S-Curve
    channel,        // Omni, 1..16: a choice of 17
    note,           // Trigger: the 128 notes by name, C1 = 36
    midiMode,       // Mode: Trigger / Gate
    velSens,        // Vel: 0..100 %
    threshold,      // -60..0 dB, the bottom reading "-inf dB"
    lockout,        // 0..200 ms
    count
};
} // namespace param

/* What fires the duck: Source's three options, in order. */
enum class Source : int
{
    cycle = 0,
    midi = 1,
    sidechain = 2,
};

/* Where the envelope is: params.rs's stage_index. */
enum class Stage : int
{
    idle = 0,
    delay,
    attack,
    hold,
    release,
};

/*
 * THE ENGINE'S `ui` READOUT: what changes on its own, once a frame.
 *
 *   source       the source the engine runs (the parameter's, as it took it)
 *   rate         the rate's index into the Rate parameter's choices
 *   msPerCycle   one cycle in ms: the well's span; 0 before a tempo is known
 *   sweep        how far through the cycle the capture is, 0..1: the
 *                playhead, on the shared axis of all three sources
 *   advancing    whether the cycle is moving (the transport, on Cycle)
 *   fires        how many triggers have fired: a counter that only grows, so
 *                a change says one arrived
 *   duck         the attenuation applied now, 0..1
 *   key          the sidechain key's level now, linear
 *   stage        where the envelope is
 *   phase        the transport's position in the cycle, 0..1 (Cycle only)
 */
struct State
{
    Source source = Source::cycle;
    int rate = 4;
    double msPerCycle = 0.0;
    double sweep = 0.0;
    bool advancing = false;
    std::uint32_t fires = 0;
    float duck = 0.0f;
    float key = 0.0f;
    Stage stage = Stage::idle;
    double phase = 0.0;
};

/* The four stage lengths in ms at the cycle now: the engine's `stage_ms`.
 * Delay is negative when the duck starts early. */
struct StageMs
{
    double delay = 0.0, attack = 0.0, hold = 0.0, release = 0.0;

    double total() const noexcept { return delay + attack + hold + release; }
};

/*
 * THE KEY BUS, which only the plugin can see: whether the host connected one
 * at all -- an unpatched bus and a silent one are the same zeroes.
 */
struct Buses
{
    bool keyConnected = false;
};

/*
 * THE CAPTURE: one cycle, phase-locked to the trigger, `count` columns of
 * `stride` floats -- whether the sweep has reached the column yet, the input's
 * low and high, the output's low and high (all -1..1), and the gain applied
 * (0..1). Empty (count 0) before the plugin has captured anything, which is
 * no verdict on the input.
 */
struct Scope
{
    enum Column : int { seen = 0, dryLo, dryHi, wetLo, wetHi, gain, stride };

    std::vector<float> data;
    int count = 0;

    bool isSeen (int column) const { return data[(size_t) (column * stride + seen)] >= 0.5f; }
    float at (int column, Column c) const { return data[(size_t) (column * stride + c)]; }
};

/*
 * THE SHAPE'S LANDMARKS, as positions in the cycle, 0..100 %: where the duck
 * starts (Delay's handle), reaches the bottom (Attack and Depth), leaves it
 * (Hold) and is back (Release). On Cycle they wrap -- Delay is a phase there,
 * so -20 % starts at 80 % -- and elsewhere a negative Delay is no wait at all,
 * the engine's own clamp. `span` is attack + hold + release UNWRAPPED, which
 * is what says whether the duck can finish inside one cycle. `floor` is the
 * gain the duck bottoms out at, 1 - Depth: where the two inner handles sit.
 */
struct ShapeMarks
{
    double start = 0.0, bottom = 0.0, holdEnd = 0.0, end = 0.0;
    double span = 0.0;
    double floor = 0.0;
};

class Model : public ni::ui::EditorModel
{
public:
    /* The latest `ui` readout. */
    virtual State state() = 0;

    /* The four stage lengths in ms. */
    virtual StageMs stageMs() = 0;

    /* The key bus. */
    virtual Buses buses() = 0;

    /* The capture; valid until the next call. */
    virtual const Scope& scope() = 0;

    /*
     * THE DUCK ONE TRIGGER MAKES, as the engine's shape() computes it from
     * the host parameters now: the gain 0..1 at `count` phases evenly spaced
     * from 0 to 100 % of the cycle, both ends included, Depth applied. The
     * idealised single shot -- what was asked for; the capture's gain is what
     * happened.
     */
    virtual void shapeGain (float* gain, int count) = 0;

    /* Where its handles are, from the same parameters. */
    virtual ShapeMarks shapeMarks() = 0;
};

} // namespace ni::sc

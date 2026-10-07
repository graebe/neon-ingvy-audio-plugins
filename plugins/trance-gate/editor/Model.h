// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the Trance Gate's editor needs from its plugin, and all it may ask.
 *
 * The web editor was fed through message tags (ui/src/lib/msg.js): the `ui`
 * and `params` readouts as colon-separated text every idle tick, the gate,
 * the envelope and the Signal capture as base64 bytes, and the pattern's
 * edits, the slot files and the clipboard as tagged strings back. This is the
 * same traffic as C++: snapshots the editor reads when it wants them, and
 * commands that return at once and show in a later snapshot -- no tags, no
 * text where the data is a number, no format to parse.
 *
 * MESSAGE THREAD, EVERY CALL (EditorModel.h). A getter returns the latest
 * snapshot the engine published, already copied out of its triple buffer; a
 * reference it returns stays valid until the same getter is called again. A
 * command returns at once and shows in a later snapshot, so the editor never
 * reads its own edit back from the snapshot it is about to replace. The
 * processor implements this; tests/ui/trance-gate_fakes.h implements it with
 * nothing behind it.
 *
 * WHAT IS NOT HERE: the clipboard and the file panels. They are the
 * operating system's, used from the editor's side (Clipboard.h,
 * FilePanels.h); the model only turns a slot into text and text into slots.
 *
 * THE HOST PARAMETERS ARE THE PLUGIN'S FIFTEEN, in the iPlug2 index order
 * that is their VST3 ID (Params.h, and the spike's
 * tests/fixtures/iplug2/NITranceGate/parameters.json): parameter(param::rate)
 * is Rate. Bypass is the host's and is not among them. Their text is the
 * plugin's (the engine's rate labels, "100.00 %"), and the editor holds no
 * unit, precision or label of its own.
 */
#pragma once

#include "EditorModel.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ni::tg
{

/* The host parameters' indices: tg_param_t's order, so the host index is the
 * engine index. Fade and its two switches were APPENDED, which is why they
 * are not beside Amount. These are the VST3 parameter IDs 0..14 of the
 * fixture; Bypass, 65536 there, is the host's. */
namespace param
{
enum : int
{
    slot = 0,       // 1..8, a stepped integer
    length,         // 1..128 steps, a stepped integer
    rate,           // a choice of 13, the engine's labels ("1/16")
    legato,         // Join Neighbors, Off/On
    timeMode,       // Env Time, "ms" / "%"
    curve,          // Env Curve, Linear / Exponential / S-Curve
    amount,         // 0..100 %
    width,          // 5..100 %
    attack,         // 0..200 % of the gate's open time
    decay,          // 0..200 %
    sustain,        // 0..100 %
    release,        // 0..200 %
    fade,           // 0..100 %
    fadeSoft,       // Fade Shape, Hard/Soft
    fadeDir,        // Fade Dir, In/Out
    count
};
} // namespace param

inline constexpr int maxSteps = 128;
inline constexpr int numSlots = 8;
/* The longest slot or bank file the engine reads (TG_SLOTFILE_MAX). */
inline constexpr int maxFileBytes = 16384;

/* What a step was drawn as: off, on, or tied -- a tie carries the step
 * before it on through this one, and counts as drawn on. The engine's own
 * values (tg_step_t), which the Move's Gate parameter names Off, On and Tie. */
enum class StepMode : int
{
    off = 0,
    on = 1,
    tie = 2,
};

/*
 * THE CURRENT SLOT'S PATTERN: the engine's `ui` readout. Steps past `length`
 * are kept by the engine and mean nothing here.
 */
struct Pattern
{
    int length = 16;                        // 1..maxSteps
    int cursor = 0;                         // the step being edited, drawn as a bracket
    std::array<StepMode, maxSteps> steps {};
    std::array<float, maxSteps> depths {};  // each step's amount, 0..1
    /* Each step's place in the fade's arrival order, ranked among its own
     * kind (the hits among the hits, the holes among the holes), 1..n; 0 for
     * none. */
    std::array<int, maxSteps> orders {};
    /*
     * Each step's LEVEL FACTOR from the fade, 0..1: the engine's own fade_w
     * (Instance::recalc_fade), which it multiplies into the step's level.
     * Published with the pattern, so the pads, the ring and the arrival
     * numbers draw what the engine plays and the editor keeps no second copy
     * of the formula. 1 is a step that sounds as drawn, 0 a gap -- whatever
     * the step was drawn as: under Fade Out a hole not yet removed is above 0.
     */
    std::array<float, maxSteps> levels {};

    bool operator== (const Pattern& o) const
    {
        return length == o.length && cursor == o.cursor && steps == o.steps && depths == o.depths
            && orders == o.orders && levels == o.levels;
    }
    bool operator!= (const Pattern& o) const { return ! (*this == o); }
};

/*
 * WHERE THE TRANSPORT IS, NOW. The processor extrapolates from the last block's
 * song position with the time since, so a call per display frame glides; the
 * editor keeps no clock of its own.
 */
struct Transport
{
    bool playing = false;
    double phase = 0.0;       // steps into the pattern, 0 <= phase < length
    double msPerStep = 0.0;   // one step at the current Rate and tempo; 0 unknown
};

/*
 * A CURVE THE ENGINE RENDERED through a scratch copy of itself: the gate across
 * one cycle (tg_core_render_gate), a byte of gain a sample once was, now the
 * floats. `serial` changes whenever the samples do, so the editor redraws a
 * plot only when there is something new in it.
 */
struct GateCurve
{
    std::uint32_t serial = 0;
    int steps = 0;                 // the pattern's length it was rendered at
    int perStep = 0;               // samples per step
    std::vector<float> values;     // steps * perStep gains, 0..1, Amount left at 1
};

/* The envelope plot's two curves (tg_core_render_envelope), in fractions of a
 * step from the gate opening: one gate as it sounds, and the envelope as
 * dialled with no gate over it. */
struct EnvelopeCurves
{
    std::uint32_t serial = 0;
    int steps = 0;
    int perStep = 0;
    std::vector<float> gated;      // steps * perStep, 0..1
    std::vector<float> ghost;      // the same length
};

/*
 * THE SIGNAL CAPTURE: one cycle of the pattern, column k at phase k / columns,
 * four values a column -- dry low, dry high, gated low, gated high, -1..1 --
 * written left to right; `head` is the column being written. Overwritten in
 * place every block, so `serial` moves while the transport plays.
 */
struct Capture
{
    static constexpr int stride = 4;

    std::uint32_t serial = 0;
    int columns = 0;
    double cycleMs = 0.0;          // one cycle at the current tempo
    int head = 0;
    std::vector<float> data;       // columns * stride
};

/* What a paste or an import did with the text it was given. */
struct Transfer
{
    enum class Kind
    {
        refused,   // nothing changed; `reason` is the engine's
        slot,      // replaced the current slot
        bank,      // replaced all eight
        patch,     // a whole patch: every slot and every setting (paste only)
    };

    Kind kind = Kind::refused;
    std::string reason;
};

class Model : public ni::ui::EditorModel
{
public:
    /* ---- snapshots */

    virtual const Pattern& pattern() = 0;
    virtual Transport transport() = 0;
    virtual const GateCurve& gate() = 0;
    virtual const EnvelopeCurves& envelope() = 0;
    virtual const Capture& capture() = 0;

    /* The Length detents: the lengths, in steps, that are half a bar, one,
     * two and four at this slot's Rate and the host's time signature, as the
     * engine counts them (rates::detents), ascending. */
    virtual std::vector<int> lengthDetents() = 0;

    /*
     * A stage's readout (attack, decay, release) in whichever unit Env Time
     * asks for: "12.5 ms" or "12.50 %" (Params.cpp FormatStage). The host's
     * own text for these stays in percent; this is the editor's.
     */
    virtual juce::String stageText (int stageParam) = 0;
    /* Typed text for a stage, read as the plugin reads it (ParseStage): the
     * unit typed wins, a bare number is in Env Time's unit. The normalised
     * value, or nothing for text that is not a number. */
    virtual std::optional<float> stageValue (int stageParam, const juce::String& typed) = 0;

    /* ---- the pattern's edits: none is a host parameter */

    /* Off, on or tie for one step; the engine moves the cursor there. */
    virtual void setStep (int index, StepMode) = 0;
    /* A step's amount, 0..1. */
    virtual void setDepth (int index, float amount) = 0;
    /* A step's place in the fade's order, 1..n among its kind; the engine
     * swaps it with the step that held that place and keeps a permutation. */
    virtual void setOrder (int index, int rank) = 0;
    /* A new Euclidean pattern and arrival order for the current slot, from
     * the engine's own generator, so two presses differ. */
    virtual void randomize() = 0;
    /* A random arrival order for the steps the fade is bringing in (the hits,
     * or under Fade Out the holes), the pattern left as it is: the plugin
     * deals a permutation and ranks the steps down it with setOrder, as the
     * web editor did, so two presses differ and the editor holds no dice. */
    virtual void shuffleOrder() = 0;

    /* ---- slots as text: what a file and the clipboard carry */

    /* The current slot -- or with `bank`, all eight -- as a slot or bank
     * file's text, as the next block will hold it, the host's values
     * included. Empty while the plugin cannot say. */
    virtual std::string exportText (bool bank) = 0;
    /* A file's text: a slot file into the current slot, a bank into all
     * eight. Never a patch. */
    virtual Transfer importText (const std::string& text) = 0;
    /* The clipboard's text: a slot, a bank or a whole patch, as the engine
     * decides. */
    virtual Transfer paste (const std::string& text) = 0;

    /* The folder the last slot file was saved to or opened from, where the
     * next panel opens (README, Slot files). The processor's, so it outlives
     * the editor; per instance and never in the host's state. A default
     * File for none yet. */
    virtual juce::File lastFolder() = 0;
    virtual void setLastFolder (const juce::File&) = 0;
};

} // namespace ni::tg

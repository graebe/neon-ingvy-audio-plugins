// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor's model, answered by the engine. EngineModel.h says how.
 */
#include "EngineModel.h"

#include "MachineSettings.h"
#include "TranceGate.h"
#include "ni/Wire.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ni::tg
{

namespace
{
/* How long the playhead glides past the engine's last publish before it
 * waits for the next one: a few publishes' worth, never a runaway. */
constexpr double glideMs = 200.0;

std::vector<std::string> fields (const std::string& text)
{
    std::vector<std::string> out (1);
    for (const char c : text)
    {
        if (c == ':')
            out.emplace_back();
        else
            out.back() += c;
    }
    return out;
}

int hexDigit (char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/* The engine's mask hex: the least significant bit is the END of the string. */
bool bitOf (const std::string& hex, int bit)
{
    const int digit = (int) hex.size() - 1 - bit / 4;
    if (digit < 0)
        return false;
    const int v = hexDigit (hex[(std::size_t) digit]);
    return v > 0 && ((v >> (bit % 4)) & 1) != 0;
}

/* Two hex digits a step. */
int byteAt (const std::string& run, int index)
{
    const auto at = (std::size_t) index * 2;
    if (at + 2 > run.size())
        return 0;
    const int hi = hexDigit (run[at]), lo = hexDigit (run[at + 1]);
    return hi < 0 || lo < 0 ? 0 : hi * 16 + lo;
}

/* "<a>:<b>:" at the front of a render, and where its samples start. */
bool renderHeader (const char* data, int size, int& a, int& b, int& at)
{
    int colons = 0, value = 0;
    int got[2] {};
    for (int i = 0; i < size && i < 32; ++i)
    {
        const char c = data[i];
        if (c == ':')
        {
            got[colons++] = value;
            value = 0;
            if (colons == 2)
            {
                a = got[0];
                b = got[1];
                at = i + 1;
                return a > 0 && b > 0;
            }
        }
        else if (c >= '0' && c <= '9')
        {
            value = value * 10 + (c - '0');
        }
        else
        {
            return false;
        }
    }
    return false;
}

float gainOf (char byte)
{
    return (float) (unsigned char) byte / 255.0f;
}

double nowMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}

std::string readout (tg_shell_t* shell, const char* key)
{
    std::string text (TG_STATE_MAX, '\0');
    const int n = tg_shell_read (shell, key, text.data(), (int) text.size());
    text.resize (n > 0 ? (std::size_t) n : 0);
    return text;
}
} // namespace

EngineModel::EngineModel (Processor& p) : processor (p) {}

EngineModel::~EngineModel() = default;

int EngineModel::numParameters() const
{
    return kNumParams;
}

juce::RangedAudioParameter& EngineModel::parameter (int index)
{
    return processor.parameter (index);
}

int EngineModel::takeRings (float* strengths, int capacity)
{
    return processor.ground().takeRings (strengths, capacity);
}

bool EngineModel::motion() const
{
    return ni::MachineSettings::motion ("trance-gate");
}

void EngineModel::setMotion (bool on)
{
    ni::MachineSettings::setMotion ("trance-gate", on);
}

int EngineModel::currentSlot() const
{
    return (int) std::lround (processor.parameter (kSlot).plain());
}

/* ------------------------------------------------------------ snapshots -- */

/*
 * "steps:ties:length:phase:ms_step:advancing:cursor:depths:orders:slot", the
 * engine's `ui` readout (tg-core params.rs): masks in hex, the depths and the
 * arrival ranks two hex digits a step.
 */
bool EngineModel::readUi()
{
    auto text = readout (processor.engine(), "ui");
    if (text == uiText)
        return false;
    uiText = std::move (text);
    const auto f = fields (uiText);
    if (f.size() < 9)
        return true;

    Pattern p;
    p.length = juce::jlimit (1, maxSteps, std::atoi (f[2].c_str()));
    p.cursor = std::atoi (f[6].c_str());
    for (int i = 0; i < maxSteps; ++i)
    {
        const auto at = (std::size_t) i;
        const bool on = bitOf (f[0], i);
        p.steps[at] = ! on ? StepMode::off : bitOf (f[1], i) ? StepMode::tie : StepMode::on;
        p.depths[at] = i < p.length ? (float) byteAt (f[7], i) / 255.0f : 0.0f;
        p.orders[at] = i < p.length ? byteAt (f[8], i) : 0;
    }
    p.levels = shown.levels;
    shown = p;

    phase = ni::wire::parse_number (f[3]);
    msPerStep = ni::wire::parse_number (f[4]);
    advancing = f[5] == "1";
    phaseAt = nowMs();
    return true;
}

const Pattern& EngineModel::pattern()
{
    readUi();
    /* The levels move with Fade and its switches, which are not in the `ui`
     * readout: read every time, a copy of a fixed array. */
    tg_shell_levels (processor.engine(), shown.levels.data(), maxSteps);
    for (int i = shown.length; i < maxSteps; ++i)
        shown.levels[(std::size_t) i] = 0.0f;
    return shown;
}

Transport EngineModel::transport()
{
    readUi();
    Transport t;
    t.playing = advancing;
    t.msPerStep = msPerStep;
    t.phase = phase;
    if (advancing && msPerStep > 0.0)
    {
        const double since = juce::jlimit (0.0, glideMs, nowMs() - phaseAt);
        t.phase = std::fmod (phase + since / msPerStep, (double) shown.length);
    }
    return t;
}

/*
 * "slot:legato:time_mode:curve:rate:length:amount:hold:attack:decay:sustain:
 * release:width_ms:fade:fade_soft:fade_dir:detents", the `params` readout.
 */
bool EngineModel::readParams()
{
    auto text = readout (processor.engine(), "params");
    if (text == paramsText)
        return false;
    paramsText = std::move (text);
    const auto f = fields (paramsText);
    widthMs = f.size() > 12 ? ni::wire::parse_number (f[12]) : 0.0;
    detents.clear();
    if (f.size() > 16)
    {
        int value = 0;
        bool any = false;
        for (const char c : f[16] + ",")
        {
            if (c >= '0' && c <= '9')
            {
                value = value * 10 + (c - '0');
                any = true;
            }
            else if (any)
            {
                if (value > 0)
                    detents.push_back (value);
                value = 0;
                any = false;
            }
        }
    }
    return true;
}

std::vector<int> EngineModel::lengthDetents()
{
    readParams();
    return detents;
}

juce::String EngineModel::stageText (int stageParam)
{
    readParams();
    const bool ms = processor.parameter (kTimeMode).plain() < 0.5;
    return juce::String (formatStage (processor.parameter (stageParam).plain(), ms, widthMs));
}

std::optional<float> EngineModel::stageValue (int stageParam, const juce::String& typed)
{
    readParams();
    const bool ms = processor.parameter (kTimeMode).plain() < 0.5;
    double pct = 0.0;
    if (! parseStage (typed.toStdString(), ms, widthMs, pct))
        return std::nullopt;
    auto& p = processor.parameter (stageParam);
    return p.convertTo0to1 ((float) pct);
}

/*
 * THE TWO CURVES THE ENGINE RENDERS, from the patch it published: rendered
 * only when the patch moved, since the blob is the whole patch.
 */
void EngineModel::readRenders()
{
    auto text = readout (processor.engine(), "state");
    if (text == stateText)
        return;
    stateText = std::move (text);

    std::vector<char> buffer (TG_GATE_MAX);
    int steps = 0, perStep = 0, at = 0;
    int n = tg_core_render_gate (stateText.c_str(), buffer.data(), (int) buffer.size());
    if (n > 0 && renderHeader (buffer.data(), n, steps, perStep, at) && n - at >= steps * perStep)
    {
        gateCurve.steps = steps;
        gateCurve.perStep = perStep;
        gateCurve.values.resize ((std::size_t) (steps * perStep));
        for (int i = 0; i < steps * perStep; ++i)
            gateCurve.values[(std::size_t) i] = gainOf (buffer[(std::size_t) (at + i)]);
        ++gateCurve.serial;
    }

    buffer.assign (TG_ENVELOPE_MAX, 0);
    n = tg_core_render_envelope (stateText.c_str(), buffer.data(), (int) buffer.size());
    if (n > 0 && renderHeader (buffer.data(), n, steps, perStep, at) && n - at >= 2 * steps * perStep)
    {
        const int count = steps * perStep;
        envelopeCurves.steps = steps;
        envelopeCurves.perStep = perStep;
        envelopeCurves.gated.resize ((std::size_t) count);
        envelopeCurves.ghost.resize ((std::size_t) count);
        for (int i = 0; i < count; ++i)
        {
            envelopeCurves.gated[(std::size_t) i] = gainOf (buffer[(std::size_t) (at + i)]);
            envelopeCurves.ghost[(std::size_t) i] = gainOf (buffer[(std::size_t) (at + count + i)]);
        }
        ++envelopeCurves.serial;
    }
}

const GateCurve& EngineModel::gate()
{
    readRenders();
    return gateCurve;
}

const EnvelopeCurves& EngineModel::envelope()
{
    readRenders();
    return envelopeCurves;
}

/* The processor's scope as it stands: four floats a column, written in place
 * by the audio thread while the window is open. A new serial only when
 * something in it moved. */
const Capture& EngineModel::capture()
{
    const auto& scope = processor.scope();
    constexpr int columns = Processor::scopeColumns;
    scratch.resize ((std::size_t) (columns * Capture::stride));
    for (int c = 0; c < columns; ++c)
    {
        float column[5];
        scope.ReadColumn (c, column);
        std::copy (column, column + Capture::stride, scratch.begin() + c * Capture::stride);
    }
    const int head = scope.Head();
    const double cycleMs = tg_shell_cycle_ms (processor.engine());
    if (scratch != signal.data || head != signal.head || ! juce::exactlyEqual (cycleMs, signal.cycleMs)
        || signal.columns != columns)
    {
        signal.data.swap (scratch);
        signal.columns = columns;
        signal.head = head;
        signal.cycleMs = cycleMs;
        ++signal.serial;
    }
    return signal;
}

/* ------------------------------------------------------------- commands -- */

bool EngineModel::post (std::initializer_list<std::string> pairs)
{
    std::vector<const char*> kv;
    for (const auto& s : pairs)
        kv.push_back (s.c_str());
    return tg_shell_post (processor.engine(), kv.data(), (int) kv.size() / 2) == 1;
}

/*
 * The cursor moves first because `step`, `step_amount` and `step_order` edit
 * whatever it is on -- in ONE post, so nothing can move it in between.
 */
void EngineModel::setStep (int index, StepMode mode)
{
    if (post ({ "cursor", std::to_string (index), "step", std::to_string ((int) mode) }))
        processor.stateChanged();
}

void EngineModel::setDepth (int index, float amount)
{
    std::string value;
    ni::wire::append_decimal (value, juce::jlimit (0.0f, 1.0f, amount), 9);
    if (post ({ "cursor", std::to_string (index), "step_amount", value }))
        processor.stateChanged();
}

void EngineModel::setOrder (int index, int rank)
{
    if (post ({ "cursor", std::to_string (index), "step_order", std::to_string (std::max (1, rank)) }))
        processor.stateChanged();
}

/* The engine's own generator; the shell gives the roll its seed, so the roll
 * that plays is the one a save writes. */
void EngineModel::randomize()
{
    if (post ({ "randomize", "" }))
        processor.stateChanged();
}

/*
 * SHUFFLE, as a run of ranks down a shuffled list -- the web editor's way:
 * each rank inserts and shifts, so the engine needs nothing Set order does
 * not already use. The steps it deals are the ones the fade brings in: the
 * hits, or under Fade Out the holes.
 */
void EngineModel::shuffleOrder()
{
    const auto& p = pattern();
    const bool fadeOut = processor.parameter (kFadeDir).plain() >= 0.5;
    std::vector<int> arriving;
    for (int i = 0; i < p.length; ++i)
        if ((p.steps[(std::size_t) i] != StepMode::off) != fadeOut)
            arriving.push_back (i);
    auto& random = juce::Random::getSystemRandom();
    for (int i = (int) arriving.size() - 1; i > 0; --i)
        std::swap (arriving[(std::size_t) i], arriving[(std::size_t) random.nextInt (i + 1)]);
    bool any = false;
    for (std::size_t k = 0; k < arriving.size(); ++k)
        any = post ({ "cursor", std::to_string (arriving[k]), "step_order", std::to_string (k + 1) }) || any;
    if (any)
        processor.stateChanged();
}

std::string EngineModel::exportText (bool bank)
{
    double values[kNumParams];
    processor.engineValues (values);
    std::string text (TG_SLOTFILE_MAX, '\0');
    const int n = tg_shell_export (processor.engine(), values, kNumParams, bank ? 1 : 0, text.data(), (int) text.size());
    text.resize (n > 0 ? (std::size_t) n : 0);
    return text;
}

namespace
{
Transfer refused (const char* err)
{
    Transfer t;
    t.kind = Transfer::Kind::refused;
    t.reason = err[0] != '\0' ? std::string (err) : std::string ("the plugin is not ready.");
    return t;
}
} // namespace

Transfer EngineModel::importText (const std::string& text)
{
    char err[256] {};
    const int kind = tg_shell_import (processor.engine(), currentSlot() - 1, text.c_str(), err, (int) sizeof err);
    if (kind == 0)
        return refused (err);
    processor.stateChanged();
    return { kind == TG_PASTE_SLOT ? Transfer::Kind::slot : Transfer::Kind::bank, {} };
}

Transfer EngineModel::paste (const std::string& text)
{
    /* Past the largest text the engine reads, the length only has to stay
     * past it: the engine refuses it whole, in its own words. */
    const int len = (int) std::min<std::size_t> (text.size(), (std::size_t) TG_SLOTFILE_MAX + 1);
    char err[256] {};
    const int holds = tg_shell_paste (processor.engine(), currentSlot() - 1, text.data(), len, err, (int) sizeof err);
    switch (holds)
    {
        case TG_PASTE_SLOT:  processor.stateChanged(); return { Transfer::Kind::slot, {} };
        case TG_PASTE_BANK:  processor.stateChanged(); return { Transfer::Kind::bank, {} };
        case TG_PASTE_PATCH: processor.stateChanged(); return { Transfer::Kind::patch, {} };
        default:             return refused (err);
    }
}

} // namespace ni::tg

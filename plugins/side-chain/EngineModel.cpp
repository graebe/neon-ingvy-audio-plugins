// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor's model, answered by the engine. EngineModel.h says how.
 */
#include "EngineModel.h"

#include "MachineSettings.h"
#include "SideChain.h"
#include "ni/Wire.h"

#include <cmath>
#include <vector>

namespace ni::sc
{

namespace
{
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

int integer (const std::string& s)
{
    int v = 0;
    return ni::wire::parse_int (s, v) ? v : 0;
}

std::string readout (sc_shell_t* shell, const char* key)
{
    std::string text (SC_STATE_MAX, '\0');
    const int n = sc_shell_read (shell, key, text.data(), (int) text.size());
    text.resize (n > 0 ? (std::size_t) n : 0);
    return text;
}

/* The single shot's settings, in the engine's units, from the host's values. */
sc_shape_t shapeOf (const Processor& p)
{
    const auto plain = [&p] (int i) { return p.parameter (i).plain(); };
    sc_shape_t s {};
    s.curve = (int) std::lround (plain (kCurve));
    s.delay = plain (kDelay);
    s.attack = plain (kAttack);
    s.hold = plain (kHold);
    s.release = plain (kRelease);
    s.depth = toEngine (kDepth, plain (kDepth));
    s.cycle = std::lround (plain (kSource)) == 0 ? 1 : 0;
    return s;
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
    return ni::MachineSettings::motion ("side-chain");
}

void EngineModel::setMotion (bool on)
{
    ni::MachineSettings::setMotion ("side-chain", on);
}

/*
 * "source:rate:ms_cycle:sweep:advancing:fires:duck:key:connected:stage:phase",
 * the engine's `ui` readout (sc-core params.rs): eleven fields, by position.
 * Re-read only when its text moved.
 */
State EngineModel::state()
{
    auto text = readout (processor.engine(), "ui");
    if (text == uiText)
        return shown;
    uiText = std::move (text);
    const auto f = fields (uiText);
    if (f.size() < 11)
        return shown;

    State s;
    s.source = (Source) juce::jlimit (0, 2, integer (f[0]));
    s.rate = integer (f[1]);
    s.msPerCycle = ni::wire::parse_number (f[2]);
    s.sweep = ni::wire::parse_number (f[3]);
    s.advancing = f[4] == "1";
    s.fires = (std::uint32_t) ni::wire::parse_number (f[5]);
    s.duck = (float) ni::wire::parse_number (f[6]);
    s.key = (float) ni::wire::parse_number (f[7]);
    s.stage = (Stage) juce::jlimit (0, 4, integer (f[9]));
    s.phase = ni::wire::parse_number (f[10]);
    shown = s;
    return shown;
}

/* "delay:attack:hold:release" in ms, the engine's `stage_ms`. */
StageMs EngineModel::stageMs()
{
    auto text = readout (processor.engine(), "stage_ms");
    if (text == stagesText)
        return stages;
    stagesText = std::move (text);
    const auto f = fields (stagesText);
    if (f.size() >= 4)
        stages = { ni::wire::parse_number (f[0]), ni::wire::parse_number (f[1]), ni::wire::parse_number (f[2]),
                   ni::wire::parse_number (f[3]) };
    return stages;
}

Buses EngineModel::buses()
{
    return { processor.keyConnected() };
}

/*
 * The processor's capture as it stands, six floats a column: whether the
 * sweep has reached it, the input's and the output's bounds, the gain. Empty
 * until a block has written a column since the window opened -- no verdict
 * on the input, which a column of silence would be.
 */
const Scope& EngineModel::scope()
{
    const auto& capture = processor.scope();
    constexpr int columns = Processor::scopeColumns;
    signal.data.resize ((std::size_t) (columns * Scope::stride));
    bool any = false;
    for (int c = 0; c < columns; ++c)
    {
        float column[5];
        capture.ReadColumn (c, column);
        const bool seen = capture.Seen (c);
        any = any || seen;
        auto* out = signal.data.data() + c * Scope::stride;
        out[Scope::seen] = seen ? 1.0f : 0.0f;
        out[Scope::dryLo] = column[0];
        out[Scope::dryHi] = column[1];
        out[Scope::wetLo] = column[2];
        out[Scope::wetHi] = column[3];
        out[Scope::gain] = column[4];
    }
    signal.count = any ? columns : 0;
    return signal;
}

void EngineModel::shapeGain (float* out, int count)
{
    const auto s = shapeOf (processor);
    sc_shape_render (&s, out, count);
}

ShapeMarks EngineModel::shapeMarks()
{
    const auto s = shapeOf (processor);
    sc_shape_marks_t m {};
    if (sc_shape_marks (&s, &m) != 0)
        return {};
    return { m.start, m.bottom, m.hold_end, m.end, m.span, m.floor };
}

} // namespace ni::sc

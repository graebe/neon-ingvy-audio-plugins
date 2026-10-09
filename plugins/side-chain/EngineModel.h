// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Side-Chain editor's model (editor/Model.h), answered by the engine.
 *
 * SNAPSHOTS ARE WHAT THE ENGINE PUBLISHED. The audio thread publishes the
 * engine's readouts into the shell's triple buffer at the end of a block
 * (sc_shell.h); a getter here reads the latest -- the `ui` readout into the
 * State, `stage_ms` into the stage lengths -- and the processor's scope and
 * key bus as they stand. The web editor got the same traffic as text through
 * message tags; here it is plain C++.
 *
 * THE SHAPE IS THE ENGINE'S, from the host parameters NOW: sc_shape_render
 * and sc_shape_marks need no engine instance, so a dragged handle is under
 * the pointer at once, without waiting for a block to publish it.
 *
 * ONE EDIT THAT IS NOT A PARAMETER: the kick the plot shows (Kick.h), which
 * the set keeps and the host is told of. Everything else the Side-Chain holds
 * is a host parameter; the editor's edits go to those through their bindings.
 *
 * THE KICK'S BUSES ARE PROBED HERE, every half second -- the Spectrogram's
 * cadence -- and the processor's reader is serviced every call, which is
 * every frame of an open window: nothing needs one while none is open.
 *
 * Owned by the processor. Message thread, every call.
 */
#pragma once

#include "Model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ni::sc
{

class Processor;

class EngineModel final : public Model
{
public:
    explicit EngineModel (Processor&);
    ~EngineModel() override;

    int numParameters() const override;
    juce::RangedAudioParameter& parameter (int index) override;
    int takeRings (float* strengths, int capacity) override;
    bool motion() const override;
    void setMotion (bool on) override;

    State state() override;
    StageMs stageMs() override;
    Buses buses() override;
    const Scope& scope() override;
    KickView kick() override;
    void chooseKick (int choice) override;
    void shapeGain (float* gain, int count) override;
    ShapeMarks shapeMarks() override;

private:
    Processor& processor;

    std::string uiText, stagesText;
    State shown;
    StageMs stages;
    Scope signal;
    static constexpr std::uint32_t pollMs = 500;
    /* The Listen-In buses, probed every pollMs: sixteen system calls are not a frame's
     * work. */
    std::vector<ni::ui::BusSource> listenIns;
    std::uint32_t polled = 0;

    JUCE_DECLARE_NON_COPYABLE (EngineModel)
};

} // namespace ni::sc

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate editor's model (editor/Model.h), answered by the engine.
 *
 * SNAPSHOTS ARE WHAT THE ENGINE PUBLISHED. The audio thread publishes the
 * engine's readouts into the shell's triple buffer at the end of a block
 * (tg_shell.h); a getter here reads the latest one -- or, while an edit is
 * still queued, the engine as it will be once it lands -- and turns it into
 * the plain C++ the editor draws: the `ui` readout into the Pattern, the
 * fade's levels beside it (tg_shell_levels), the `params` readout into the
 * detents and the gate's width in ms, the `state` blob into the two curves
 * the engine renders (tg_core_render_gate, tg_core_render_envelope), and the
 * processor's scope into the Signal capture. Each is re-read only when its
 * text moved, and handed over by reference until the next call.
 *
 * COMMANDS GO THROUGH THE ENGINE'S QUEUE. A step, a depth, an order or a roll
 * is posted (tg_shell_post), read into a typed edit on this side and applied
 * by the audio thread at the top of its next block; a paste or an import is
 * checked whole here and queued only when good. Each marks the set unsaved,
 * since none of them is a host parameter.
 *
 * THE TRANSPORT GLIDES. The readout's phase is the engine's at its last
 * publish; transport() carries it on with the time since, at the engine's
 * step length, for at most a fifth of a second -- so a playhead moves on every
 * frame between two publishes, and stops when the audio does.
 *
 * Owned by the processor, so the last folder outlives the window. Message
 * thread, every call.
 */
#pragma once

#include "Model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ni::tg
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

    const Pattern& pattern() override;
    Transport transport() override;
    const GateCurve& gate() override;
    const EnvelopeCurves& envelope() override;
    const Capture& capture() override;
    std::vector<int> lengthDetents() override;
    juce::String stageText (int stageParam) override;
    std::optional<float> stageValue (int stageParam, const juce::String& typed) override;

    void setStep (int index, StepMode) override;
    void setDepth (int index, float amount) override;
    void setOrder (int index, int rank) override;
    void randomize() override;
    void shuffleOrder() override;

    std::string exportText (bool bank) override;
    Transfer importText (const std::string& text) override;
    Transfer paste (const std::string& text) override;

    juce::File lastFolder() override { return folder; }
    void setLastFolder (const juce::File& f) override { folder = f; }

private:
    /* The readouts, re-read; each returns whether its text moved. */
    bool readUi();
    bool readParams();
    void readRenders();
    /* One edit's pairs into the queue, without marking the set. */
    bool post (std::initializer_list<std::string> pairs);
    int currentSlot() const;

    Processor& processor;

    std::string uiText, paramsText, stateText;
    Pattern shown;
    /* The readout's playhead, and when it was read. */
    double phase = 0.0, msPerStep = 0.0, phaseAt = 0.0;
    bool advancing = false;
    double widthMs = 0.0;
    std::vector<int> detents;

    GateCurve gateCurve;
    EnvelopeCurves envelopeCurves;
    Capture signal;
    std::vector<float> scratch;

    juce::File folder;

    JUCE_DECLARE_NON_COPYABLE (EngineModel)
};

} // namespace ni::tg

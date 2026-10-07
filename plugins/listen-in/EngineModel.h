// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Listen-In editor's model (editor/Model.h), answered by the processor.
 *
 * Everything it reads is already the processor's: the status the message
 * thread decided where it claims the bus, the peak the audio thread keeps in
 * an atomic, the name the session holds -- a load not yet taken included, so
 * a set loaded on the host's thread shows its name at once. The one command,
 * the name, goes to the processor, which keeps it, hands it to the bus and
 * marks the set unsaved before this returns.
 *
 * Owned by the processor. Message thread, every call.
 */
#pragma once

#include "Model.h"

namespace ni::li
{

class Processor;

class EngineModel final : public Model
{
public:
    explicit EngineModel (Processor&);

    int numParameters() const override;
    juce::RangedAudioParameter& parameter (int index) override;
    int takeRings (float* strengths, int capacity) override;
    bool motion() const override;
    void setMotion (bool on) override;

    Status status() const override;
    float peak() const override;
    juce::String label() const override;
    void setLabel (const juce::String& typed) override;

private:
    Processor& processor;

    JUCE_DECLARE_NON_COPYABLE (EngineModel)
};

} // namespace ni::li

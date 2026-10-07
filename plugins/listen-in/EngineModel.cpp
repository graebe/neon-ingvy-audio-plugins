// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor's model, answered by the processor. EngineModel.h says how.
 */
#include "EngineModel.h"

#include "ListenIn.h"
#include "MachineSettings.h"

namespace ni::li
{

EngineModel::EngineModel (Processor& p) : processor (p) {}

int EngineModel::numParameters() const
{
    return kNumParams;
}

juce::RangedAudioParameter& EngineModel::parameter (int index)
{
    jassert (index == param::bus);
    juce::ignoreUnused (index);
    return processor.busParameter();
}

int EngineModel::takeRings (float* strengths, int capacity)
{
    return processor.ground().takeRings (strengths, capacity);
}

bool EngineModel::motion() const
{
    return ni::MachineSettings::motion ("listen-in");
}

void EngineModel::setMotion (bool on)
{
    ni::MachineSettings::setMotion ("listen-in", on);
}

Status EngineModel::status() const
{
    return processor.status();
}

float EngineModel::peak() const
{
    return processor.peak();
}

juce::String EngineModel::label() const
{
    return juce::String::fromUTF8 (processor.label().c_str());
}

void EngineModel::setLabel (const juce::String& typed)
{
    processor.editLabel (typed.toStdString());
}

} // namespace ni::li

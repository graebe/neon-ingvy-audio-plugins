// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The processor every product derives from. Processor.h has what it decides.
 */
#include "Processor.h"

namespace ni
{

HostClock readClock (juce::AudioPlayHead* head)
{
    HostClock clock;
    if (head == nullptr)
        return clock;
    const auto position = head->getPosition();
    if (! position.hasValue())
        return clock;

    clock.known = true;
    clock.playing = position->getIsPlaying();
    clock.ppq = position->getPpqPosition().orFallback (0.0);
    clock.bpm = position->getBpm().orFallback (0.0);
    if (const auto meter = position->getTimeSignature())
    {
        clock.numerator = meter->numerator;
        clock.denominator = meter->denominator;
    }
    return clock;
}

Processor::Processor (const BusesProperties& buses) : juce::AudioProcessor (buses) {}

Processor::~Processor() = default;

const juce::String Processor::getName() const
{
    return JucePlugin_Name;
}

bool Processor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedNoDenormals noDenormals;
    process (buffer, midi);
}

void Processor::getStateInformation (juce::MemoryBlock& out)
{
    writeState (out);
}

void Processor::setStateInformation (const void* data, int size)
{
    if (data != nullptr && size > 0)
        readState (data, (size_t) size);
}

juce::RangedAudioParameter* Processor::parameterWithId (const juce::String& id) const
{
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (ranged->getParameterID() == id)
                return ranged;
    return nullptr;
}

void Processor::writeState (juce::MemoryBlock& out)
{
    juce::XmlElement root (stateTag);
    root.setAttribute ("product", NI_PRODUCT);
    root.setAttribute ("format", stateFormat);
    root.setAttribute ("version", NI_VERSION_STRING);
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            auto* e = root.createNewChildElement ("param");
            e->setAttribute ("id", ranged->getParameterID());
            /* The plain value -- a choice's index, a time in its unit -- so a
             * range that grows later still reads an old set the same. */
            e->setAttribute ("value", (double) ranged->convertFrom0to1 (ranged->getValue()));
        }
    copyXmlToBinary (root, out);
}

bool Processor::readState (const void* data, size_t size)
{
    const auto root = getXmlFromBinary (data, (int) size);
    if (root == nullptr || ! root->hasTagName (stateTag)
        || root->getStringAttribute ("product") != NI_PRODUCT
        || root->getIntAttribute ("format") > stateFormat)
        return false;

    for (auto* e : root->getChildWithTagNameIterator ("param"))
        if (auto* p = parameterWithId (e->getStringAttribute ("id")))
        {
            const auto plain = (float) e->getDoubleAttribute ("value", p->convertFrom0to1 (p->getDefaultValue()));
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
        }
    return true;
}

void Processor::nonParameterStateChanged()
{
    JUCE_ASSERT_MESSAGE_THREAD
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

} // namespace ni

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The processor every product derives from. Processor.h has what it decides.
 */
#include "Processor.h"

#include <limits>

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
    /* No position is not position 0: an engine anchoring bar lines to 0
     * every block would pin them to now. NaN is "none" to the engines. */
    clock.ppq = position->getPpqPosition().orFallback (std::numeric_limits<double>::quiet_NaN());
    clock.bpm = position->getBpm().orFallback (0.0);
    /* A stopped transport's position is where it will start, not where this
     * block sounds, so it is no stamp. */
    if (const auto samples = position->getTimeInSamples(); samples.hasValue() && clock.playing)
    {
        clock.timed = true;
        clock.timeInSamples = *samples;
    }
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

/*
 * THE HOST'S BYPASS ARRIVES HERE TOO. JUCE's VST3 wrapper calls
 * processBlockBypassed only for a processor without a bypass parameter of its
 * own; a product with one (getBypassParameter) gets every block here, that
 * parameter on or off, and is trusted to bypass itself. So the parameter is
 * read once, for every product: on, the block is processBypassed's, exactly
 * as a host that bypasses the plugin itself would have it.
 */
void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedNoDenormals noDenormals;
    if (const auto* bypass = getBypassParameter(); bypass != nullptr && bypass->getValue() >= 0.5f)
        processBypassed (buffer, midi);
    else
        process (buffer, midi);
}

void Processor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedNoDenormals noDenormals;
    processBypassed (buffer, midi);
}

void Processor::processBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::AudioProcessor::processBlockBypassed (buffer, midi);
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

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The processor every product derives from. Processor.h has what it decides.
 */
#include "Processor.h"

#include <cmath>
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
    if (const auto meter = position->getTimeSignature())
    {
        clock.numerator = meter->numerator;
        clock.denominator = meter->denominator;
    }
    clock.looping = position->getIsLooping();
    if (const auto loop = position->getLoopPoints())
    {
        clock.loopStart = loop->ppqStart;
        clock.loopEnd = loop->ppqEnd;
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
 * THE SHELL FIRST, THEN THE PRODUCT, so a product's prepare() may report a
 * latency the fade then sees.
 *
 * THE FADE HOLDS THE INPUT AS IT CAME, UNDELAYED: right for a product that
 * adds no latency, which is every one -- and what JUCE's own bypass assumes,
 * asserting the same (AudioProcessor::processBypassed). A product that reports
 * latency has to delay this input to match before it may; until then its
 * Bypass switches at the block rather than fade against a misaligned input.
 * The input's room is one announced block of every output, allocated here and
 * never on the audio thread. The fade starts where the Bypass stands, so a
 * set reopened bypassed does not fade in.
 */
void Processor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    prepare (sampleRate, maximumExpectedSamplesPerBlock);

    jassert (getLatencySamples() == 0);
    const bool fades = getBypassParameter() != nullptr && ! passesAudioThrough() && getLatencySamples() == 0;
    dryChannels = fades ? juce::jmax (1, getTotalNumOutputChannels()) : 0;
    dryFrames = fades ? juce::jmax (1, maximumExpectedSamplesPerBlock) : 0;
    dry.assign ((std::size_t) dryChannels * (std::size_t) dryFrames, 0.0f);
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    bypassStep = (float) (1.0 / juce::jmax (1.0, std::round (rate * bypassFadeSeconds)));
    bypassMix = bypassParameterOn() ? 1.0f : 0.0f;
}

bool Processor::bypassParameterOn() const
{
    const auto* bypass = getBypassParameter();
    return bypass != nullptr && bypass->getValue() >= 0.5f;
}

/*
 * THE HOST'S BYPASS ARRIVES HERE TOO. JUCE's VST3 wrapper calls
 * processBlockBypassed only for a processor without a bypass parameter of its
 * own; a product with one (getBypassParameter) gets every block here, that
 * parameter on or off, and is trusted to bypass itself. So the parameter is
 * read once, for every product: on, the block is processBypassed's, exactly
 * as a host that bypasses the plugin itself would have it.
 *
 * AND IT FADES. Switched at a block, a shut gate and a Bypass going on jump
 * the output from silence to full scale in one sample. So while the output is
 * not yet where the Bypass is, the block is process()'s with the input faded
 * over it (processFading) -- 10 ms end to end at any rate, and a Bypass that
 * turns back half way turns the fade back from where it stood. At rest the
 * block is process()'s alone or processBypassed()'s alone, as it always was:
 * bypassed is the input bit for bit.
 */
void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedNoDenormals noDenormals;
    const bool bypassed = bypassParameterOn();
    const float at = bypassed ? 1.0f : 0.0f;
    if (dryChannels > 0 && ! juce::exactlyEqual (bypassMix, at))
    {
        processFading (buffer, midi, bypassed);
        return;
    }
    bypassMix = at;
    if (bypassed)
        processBypassed (buffer, midi);
    else
        process (buffer, midi);
}

/*
 * ONE BLOCK OF THE FADE. The input is what JUCE's bypass passes -- the main
 * input's channels, silence on any output past them -- kept before process()
 * writes over it. Each sample is then the processed one moved towards the
 * input by where the fade stands, linearly: w + (d - w) x, which is w itself
 * where the input is w, so a product that passes its audio is untouched by
 * it; at either end the sample is exactly the one side.
 *
 * A host that hands over a longer block than it announced gets the switch at
 * the block, as before there was a fade: keeping more input would allocate.
 */
void Processor::processFading (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed)
{
    const int frames = buffer.getNumSamples();
    if (frames > dryFrames)
    {
        bypassMix = bypassed ? 1.0f : 0.0f;
        if (bypassed)
            processBypassed (buffer, midi);
        else
            process (buffer, midi);
        return;
    }

    const int outputs = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels(), dryChannels);
    const int inputs = getMainBusNumInputChannels();
    for (int ch = 0; ch < outputs; ++ch)
    {
        float* d = dry.data() + (std::size_t) ch * (std::size_t) dryFrames;
        if (ch < inputs)
            juce::FloatVectorOperations::copy (d, buffer.getReadPointer (ch), frames);
        else
            juce::FloatVectorOperations::clear (d, frames);
    }

    process (buffer, midi);

    const float from = bypassMix;
    const float step = bypassed ? bypassStep : -bypassStep;
    for (int ch = 0; ch < outputs; ++ch)
    {
        const float* d = dry.data() + (std::size_t) ch * (std::size_t) dryFrames;
        float* out = buffer.getWritePointer (ch);
        for (int i = 0; i < frames; ++i)
        {
            const float x = juce::jlimit (0.0f, 1.0f, from + step * (float) (i + 1));
            out[i] = x >= 1.0f ? d[i] : x <= 0.0f ? out[i] : out[i] + (d[i] - out[i]) * x;
        }
    }
    bypassMix = juce::jlimit (0.0f, 1.0f, from + step * (float) frames);
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

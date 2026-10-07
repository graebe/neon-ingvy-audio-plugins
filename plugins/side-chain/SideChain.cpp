// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain on the JUCE shell. SideChain.h says what it keeps and where.
 */
#include "SideChain.h"

#include "EngineModel.h"
#include "PluginEditor.h"
#include "SideChainEditor.h"
#include "ni/Wire.h"

#include <cstring>
#include <string>

namespace ni::sc
{

namespace
{
juce::StringArray wordsOf (std::string (*word) (int), int count)
{
    juce::StringArray out;
    for (int i = 0; i < count; ++i)
        out.add (juce::String::fromUTF8 (word (i).c_str()));
    return out;
}

std::unique_ptr<ni::Parameter> make (int index)
{
    const auto& spec = specOf (index);
    /* Rate's words are the engine's table: a host stores the INDEX, so a list
     * that disagreed with the engine by one entry would re-point every lane. */
    if (index == kRate)
        return std::make_unique<ni::Parameter> (spec, wordsOf (rateLabel, numRates));
    if (index == kNote)
        return std::make_unique<ni::Parameter> (spec, wordsOf (noteName, 128));
    if (spec.kind != ParamSpec::Kind::continuous)
        return std::make_unique<ni::Parameter> (spec);
    return std::make_unique<ni::Parameter> (
        spec, juce::StringArray(), [index] (double v) { return juce::String (valueText (index, v)); },
        [index] (const juce::String& text) -> std::optional<double>
        {
            double v = 0.0;
            if (parseValue (index, text.toStdString(), v))
                return v;
            return std::nullopt;
        });
}

bool monoOrStereo (const juce::AudioChannelSet& set)
{
    return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
}
} // namespace

Processor::Processor()
    : ni::Processor (BusesProperties()
                         .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                         /* Off until a host connects a key: a bus nobody
                          * patched is not a key. */
                         .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      shell (sc_shell_create (44100.0))
{
    /* In index order: with legacy parameter IDs the index is the VST3 ID,
     * and these have to be the iPlug2 build's. */
    for (int i = 0; i < kNumParams; ++i)
    {
        auto p = make (i);
        params[(std::size_t) i] = p.get();
        addParameter (p.release());
    }
    /* LAST, so its index -- and VST3 ID -- is 15. */
    auto b = std::make_unique<ni::Parameter> (ni::bypassSpec());
    bypass = b.get();
    addParameter (b.release());

    editorModel = std::make_unique<EngineModel> (*this);
}

Processor::~Processor() = default;

/*
 * The iPlug2 build offered "1-1 1.1-1 2-2 2.1-2 2.2-2": a main input of one
 * or two channels, the output the same, and a key of one or two or none.
 * JUCE hands each bus over on its own, so any key goes with any main.
 */
bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto main = layouts.getMainInputChannelSet();
    if (! monoOrStereo (main) || layouts.getMainOutputChannelSet() != main)
        return false;
    if (layouts.inputBuses.size() < 2)
        return true;
    const auto key = layouts.getChannelSet (true, 1);
    return key.isDisabled() || monoOrStereo (key);
}

void Processor::prepareToPlay (double rate, int maximumBlock)
{
    sampleRate = rate > 0.0 ? rate : 44100.0;
    sc_shell_post_sample_rate (shell.get(), sampleRate);
    beat.prepare (sampleRate);
    /* The engine's key buffer holds SC_MAX_BLOCK, so no chunk is longer. */
    const auto n = (std::size_t) juce::jlimit (1, SC_MAX_BLOCK, maximumBlock);
    for (auto* v : { &dry, &gain, &sweep, &copyL, &copyR })
        v->assign (n, 0.0f);
    /* A column is a slice of a cycle, and the cycle may have changed length. */
    capture.Clear();
}

/*
 * THE iPlug2 SHELL'S BLOCK, on JUCE's float buffers: every parameter pushed
 * every block -- pushing on change would trust the host to report every path
 * that moves a value -- the key and the MIDI in, and the main channels ducked,
 * in chunks of what was reserved should a host hand over more.
 */
void Processor::run (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const ni::HostClock& clock,
                     bool heard)
{
    const int frames = buffer.getNumSamples();
    const int cap = (int) dry.size();
    auto main = getBusBuffer (buffer, true, 0);
    if (frames <= 0 || cap <= 0 || main.getNumChannels() < 1)
        return;
    float* left = main.getWritePointer (0);
    float* right = main.getNumChannels() > 1 ? main.getWritePointer (1) : nullptr;

    /* The key, if the host connected one: a mono key feeds both its sides. */
    const int keyChannels = getBusCount (true) > 1 ? getChannelCountOfBus (true, 1) : 0;
    const float* keyL = nullptr;
    const float* keyR = nullptr;
    if (keyChannels > 0)
    {
        const auto keyBus = getBusBuffer (buffer, true, 1);
        keyL = keyBus.getReadPointer (0);
        keyR = keyChannels > 1 ? keyBus.getReadPointer (1) : keyL;
    }
    keyed.store (keyL != nullptr, std::memory_order_relaxed);

    sc_core_t* core = sc_shell_begin (shell.get());
    for (int i = 0; i < kNumParams; ++i)
        sc_core_set_num (core, (sc_param_t) i, toEngine (i, params[(std::size_t) i]->plain()));
    sc_core_set_key_connected (core, keyL != nullptr ? 1 : 0);

    /* What a missing tempo or position means is decided once, for every
     * shell: a NaN position is none (ni::readClock). */
    const auto host = ni::wire::host_transport (clock.known && clock.playing, clock.bpm, clock.ppq);
    sc_transport_t transport { host.running, host.beats, host.bpm };

    const bool tap = heard && capturing.load (std::memory_order_relaxed);
    ni::wire::for_each_chunk (frames, cap, [&] (int off, int n)
    {
        /* The messages that fall in this chunk, at their offset in it; one
         * the host placed outside the block lands at its nearer end. */
        for (const auto m : midi)
        {
            const int at = juce::jlimit (0, frames - 1, m.samplePosition);
            if (at >= off && at < off + n)
                sc_core_on_midi (core, m.data, m.numBytes, at - off);
        }
        if (keyL != nullptr)
            sc_core_push_key_f32 (core, keyL + off, keyR + off, n);

        /* The engine takes two channels: a mono track's second is a copy of
         * its first, ducked alike and dropped; a bypassed block runs on a
         * copy of both. */
        const auto bytes = sizeof (float) * (std::size_t) n;
        float* l = left + off;
        float* r = right != nullptr ? right + off : l;
        if (! heard)
        {
            std::memcpy (copyL.data(), l, bytes);
            l = copyL.data();
        }
        if (! heard || right == nullptr)
        {
            std::memcpy (copyR.data(), r, bytes);
            r = copyR.data();
        }
        if (tap)
            std::memcpy (dry.data(), l, sizeof (float) * (std::size_t) n);
        sc_core_process_f32_split_tap (core, l, r, tap ? gain.data() : nullptr, tap ? sweep.data() : nullptr, n,
                                       &transport);
        if (tap)
            capture.Push (dry.data(), l, gain.data(), sweep.data(), n);
        /* A chunk continues the block, so the transport moves with it. */
        if (transport.running)
            transport.beats = ni::wire::advance_beats (transport.beats, n, (double) transport.bpm, sampleRate);
    });

    sc_shell_end (shell.get(), frames);
}

/*
 * THE HOST'S BYPASS IS READ HERE. JUCE's VST3 wrapper hands a processor with
 * a bypass parameter of its own every block through processBlock, that
 * parameter on or off, and leaves bypassing to it; processBlockBypassed comes
 * only from hosts that bypass a plugin themselves. Either way it is the same
 * block: the audio left as it came, the engine on a copy.
 */
void Processor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int frames = buffer.getNumSamples();
    const auto clock = readClock (getPlayHead());
    beat.tick (clock, frames);
    run (buffer, midi, clock, bypass->plain() < 0.5);
}

/* Bypassed by the host itself: audio through, its own way, while the engine
 * runs on a copy, hearing the MIDI and keeping time, and the Ground keeps the
 * host's time, so the window's beat does not stop with the sound. */
void Processor::processBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const auto clock = readClock (getPlayHead());
    beat.tick (clock, buffer.getNumSamples());
    run (buffer, midi, clock, false);
    ni::Processor::processBypassed (buffer, midi);
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new ni::PluginEditor (*this, std::make_unique<SideChainEditor> (*editorModel),
                                 ni::PluginEditor::FollowDesign {});
}

/* While a window shows them: the capture and the Ground. */
void Processor::editorOpened()
{
    capture.Retire();
    capturing.store (true, std::memory_order_relaxed);
    beat.setActive (true);
}

void Processor::editorClosed()
{
    capturing.store (false, std::memory_order_relaxed);
    beat.setActive (false);
}

/* THE CHUNK THE iPlug2 BUILD WROTE: the fifteen plain values, then the bypass
 * after it, which iPlug2 needs to load it. */
void Processor::writeState (juce::MemoryBlock& out)
{
    nist::State state;
    for (auto* p : params)
        state.params.push_back (p->plain());
    state.bypass = bypass->plain() >= 0.5;
    const auto bytes = nist::write (layout(), state);
    out.replaceAll (bytes.data(), bytes.size());
}

/* A SET REOPENED: every check before anything is applied, so a chunk no
 * build wrote changes nothing; then the values, which the next block pushes. */
bool Processor::readState (const void* data, size_t size)
{
    const auto state = nist::read (layout(), data, size);
    if (! state)
        return false;
    for (int i = 0; i < kNumParams; ++i)
        params[(std::size_t) i]->setPlainNotifyingHost (state->params[(std::size_t) i]);
    if (state->bypass)
        bypass->setPlainNotifyingHost (*state->bypass ? 1.0 : 0.0);
    return true;
}

} // namespace ni::sc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::sc::Processor();
}

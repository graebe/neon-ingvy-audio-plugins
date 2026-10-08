// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate on the JUCE shell. TranceGate.h says what it keeps and where.
 */
#include "TranceGate.h"

#include "EngineModel.h"
#include "PluginEditor.h"
#include "Window.h"
#include "ni/Wire.h"

#include <cstring>
#include <string>

namespace ni::tg
{

namespace
{
/* Rate's words: the engine's own table. A host stores the INDEX, so a list
 * that disagreed with the engine by one entry would re-point every lane. */
juce::StringArray rateLabels()
{
    juce::StringArray out;
    for (int i = 0; i < TG_NUM_RATES; ++i)
    {
        char label[32];
        out.add (tg_core_rate_label (i, label, (int) sizeof label) > 0 ? juce::String (label) : juce::String (i));
    }
    return out;
}

std::unique_ptr<ni::Parameter> make (int index)
{
    const auto& spec = specOf (index);
    if (index == kRate)
        return std::make_unique<ni::Parameter> (spec, rateLabels());
    if (spec.kind != ParamSpec::Kind::continuous)
        return std::make_unique<ni::Parameter> (spec);
    /* Every continuous one is a percentage at the host, the stages included:
     * their milliseconds are the editor's reading (Params.h). */
    return std::make_unique<ni::Parameter> (
        spec, juce::StringArray(), [] (double v) { return juce::String (percentText (v)); },
        [] (const juce::String& text) -> std::optional<double>
        {
            double v = 0.0;
            if (parsePercent (text.toStdString(), v))
                return v;
            return std::nullopt;
        });
}
} // namespace

Processor::Processor()
    : ni::Processor (BusesProperties()
                         .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      shell (tg_shell_create (44100.0))
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
    /* The slot follow: a host's idle rate, as iPlug2's was. */
    startTimerHz (30);
}

Processor::~Processor()
{
    stopTimer();
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Processor::prepareToPlay (double rate, int maximumBlock)
{
    sampleRate = rate > 0.0 ? rate : 44100.0;
    tg_shell_post_sample_rate (shell.get(), sampleRate);
    beat.prepare (sampleRate);
    const auto n = (std::size_t) juce::jmax (maximumBlock, 1);
    dry.assign (n, 0.0f);
    sweep.assign (n, 0.0f);
    /* Or the first picture after a rate change is the last session's. */
    capture.Clear();
}

void Processor::engineValues (double (&out)[kNumParams]) const noexcept
{
    for (int i = 0; i < kNumParams; ++i)
        out[i] = toEngine (i, params[(std::size_t) i]->plain());
}

/*
 * THE iPlug2 SHELL'S BLOCK, on JUCE's float buffers in place: every edit
 * posted since the last block lands at begin, the host's values are pushed
 * over it, the transport and meter go in, and the engine gates the two
 * channels -- in chunks of what was reserved, should a host hand over more.
 */
void Processor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int frames = buffer.getNumSamples();
    const int cap = (int) juce::jmin (dry.size(), sweep.size());
    const auto clock = readClock (getPlayHead());
    beat.tick (clock, frames);
    if (frames <= 0 || buffer.getNumChannels() < 2 || cap <= 0)
        return;

    double values[kNumParams];
    engineValues (values);
    tg_core_t* core = tg_shell_begin (shell.get());
    tg_shell_push (shell.get(), core, values, kNumParams);

    /* What a missing tempo or position means is decided once, for every
     * shell: a NaN position is none (ni::readClock), a missing meter 4/4. */
    const auto host = ni::wire::host_transport (clock.known && clock.playing, clock.bpm, clock.ppq);
    tg_transport_t transport { host.running, host.beats, host.bpm };
    tg_core_set_meter (core, clock.numerator > 0 ? clock.numerator : 4, clock.denominator > 0 ? clock.denominator : 4);

    const bool tap = capturing.load (std::memory_order_relaxed);
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);
    ni::wire::for_each_chunk (frames, cap, [&] (int off, int n)
    {
        if (tap)
            std::memcpy (dry.data(), left + off, sizeof (float) * (std::size_t) n);
        tg_core_process_f32_split_tap (core, left + off, right + off, tap ? sweep.data() : nullptr, n, &transport);
        if (tap)
            capture.Push (dry.data(), left + off, nullptr, sweep.data(), n);
        /* A chunk continues the block, so the transport moves with it. */
        if (transport.running)
            transport.beats = ni::wire::advance_beats (transport.beats, n, (double) transport.bpm, sampleRate);
    });

    tg_shell_end (shell.get(), frames);
}

/* Bypassed, by the host or by the Bypass -- audio through -- while the Ground
 * keeps the host's time, so the window's beat does not stop with the sound. */
void Processor::processBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    beat.tick (readClock (getPlayHead()), buffer.getNumSamples());
    ni::Processor::processBypassed (buffer, midi);
}

/*
 * A SLOT SWITCH, A PASTE OR AN IMPORT RECALLED A WHOLE SOUND; the host's
 * parameters follow it. Slot is the host's own and never set.
 */
bool Processor::followEngine()
{
    double now[kNumParams];
    if (! tg_shell_take_params (shell.get(), now, kNumParams))
        return false;
    for (int i = 0; i < kNumParams; ++i)
    {
        auto& p = *params[(std::size_t) i];
        if (i == kSlot || sameInEngine (i, p.plain(), now[i]))
            continue;
        p.beginChangeGesture();
        p.setPlainNotifyingHost (fromEngine (i, now[i]));
        p.endChangeGesture();
    }
    return true;
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new ni::PluginEditor (*this, std::make_unique<Window> (*editorModel), ni::PluginEditor::FollowDesign {});
}

/* While a window shows them: the Signal capture and the Ground. */
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

/*
 * THE CHUNK THE iPlug2 BUILD WROTE, from what the engine published: the host's
 * values, and the blob as the next block will hold it, a queued edit included
 * (tg_shell_save) -- then the bypass after it, which iPlug2 needs to load it.
 *
 * THE VALUES AGREE WITH THE BLOB. Between a Slot the host moved and the block
 * that applies it, the host still holds the slot it left, and the blob is
 * already the new one's: written as they stand, a reopened set would put the
 * old slot's sound into the new slot. So a value the next block will replace
 * -- the one the host is about to follow -- is written as that.
 */
void Processor::writeState (juce::MemoryBlock& out)
{
    nist::State state;
    for (auto* p : params)
        state.params.push_back (p->plain());
    double values[kNumParams], next[kNumParams];
    engineValues (values);
    if (tg_shell_next_params (shell.get(), values, kNumParams, next) == 1)
        for (int i = 0; i < kNumParams; ++i)
            if (i != kSlot && ! sameInEngine (i, state.params[(std::size_t) i], next[i]))
                state.params[(std::size_t) i] = fromEngine (i, next[i]);
    std::string blob (TG_STATE_MAX, '\0');
    const int length = tg_shell_save (shell.get(), values, kNumParams, blob.data(), (int) blob.size());
    blob.resize (length > 0 ? (std::size_t) length : 0);
    state.strings.push_back (std::move (blob));
    state.bypass = bypass->plain() >= 0.5;
    const auto bytes = nist::write (layout(), state);
    out.replaceAll (bytes.data(), bytes.size());
}

/*
 * A SET REOPENED, in iPlug2's order: every check before anything is applied,
 * so a chunk no build wrote changes nothing; then the parameters, which are
 * the current slot's; then the blob, every slot's pattern and sound, with
 * those parameters winning over its rounded copy of the current slot -- one
 * edit, applied at the top of the next block.
 */
bool Processor::readState (const void* data, size_t size)
{
    const auto state = nist::read (layout(), data, size);
    if (! state)
        return false;
    for (int i = 0; i < kNumParams; ++i)
        params[(std::size_t) i]->setPlainNotifyingHost (state->params[(std::size_t) i]);
    if (state->bypass)
        bypass->setPlainNotifyingHost (*state->bypass ? 1.0 : 0.0);

    /* An empty blob is a build that saved none: the engine keeps its pattern,
     * and the parameters reach it as a host's edits do, with the next block. */
    const auto& blob = state->strings.front();
    if (blob.empty())
        return true;
    double values[kNumParams];
    engineValues (values);
    tg_shell_load (shell.get(), blob.c_str(), values, kNumParams);
    return true;
}

} // namespace ni::tg

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::tg::Processor();
}

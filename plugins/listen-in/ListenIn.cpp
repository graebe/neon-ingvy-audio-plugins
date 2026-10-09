// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In on the JUCE shell. ListenIn.h says what it keeps and where.
 */
#include "ListenIn.h"

#include "EngineModel.h"
#include "ListenInEditor.h"
#include "PluginEditor.h"
#include "Wire.h"
#include "ni/Wire.h"

#include <algorithm>
#include <cmath>

namespace ni::li
{

namespace
{
/* The handoff frees through this; abus_pusher_release's pointer type is not
 * void*. */
void releasePusher (void* p)
{
    abus_pusher_release (static_cast<abus_pusher_t*> (p));
}

/* How much of the last peak a block keeps: a quiet block landing between two
 * frames would otherwise flicker the meter to nothing. */
constexpr float peakDecay = 0.85f;

/* A name as the plugin keeps it (Wire.h). */
std::string kept (const char* typed)
{
    char clean[ABUS_LABEL_CAP];
    wire::parse_label (typed, clean, (int) sizeof clean);
    return clean;
}
} // namespace

Processor::Processor()
    : ni::Processor (BusesProperties()
                         .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      handoff (shell_handoff_new (releasePusher))
{
    auto b = std::make_unique<ni::Parameter> (busSpec());
    bus = b.get();
    addParameter (b.release());
    /* LAST, so its index -- and VST3 ID -- is 1. */
    auto off = std::make_unique<ni::Parameter> (ni::bypassSpec());
    bypass = off.get();
    addParameter (off.release());

    stage.assign ((std::size_t) stageFrames * abus_channels(), 0.0f);
    named.reserve (ABUS_LABEL_CAP);
    editorModel = std::make_unique<EngineModel> (*this);
    /* The bus service: a host's idle rate, as iPlug2's was. */
    startTimerHz (30);
}

Processor::~Processor()
{
    stopTimer();
    /* No block runs any more, so the live pusher goes too, and with the
     * writer the bus. */
    shell_handoff_free (handoff);
    abus_writer_release (writer);
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

/* Recorded, not acted on: a host may prepare on any thread. */
void Processor::prepareToPlay (double sampleRate, int)
{
    rate.store ((std::uint32_t) std::lround (sampleRate > 0.0 ? sampleRate : 48000.0), std::memory_order_release);
    resetSeen.store (true, std::memory_order_release);
    beat.prepare (sampleRate > 0.0 ? sampleRate : 48000.0);
}

/*
 * THE TAP. The input is read into the stage before anything else could touch
 * the buffer, interleaved -- the bus is always stereo, so a lone channel is
 * published on both sides -- and pushed in chunks of what was reserved. The
 * buffer itself is not written: JUCE's input is its output, so the audio
 * passes through bit for bit by being left alone. A bypassed block is
 * processBypassed's (ni::Processor reads the Bypass).
 */
void Processor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int frames = buffer.getNumSamples();
    const auto clock = readClock (getPlayHead());
    beat.tick (clock, frames);
    const int channels = buffer.getNumChannels();
    if (frames <= 0 || channels < 1)
        return;

    const float* left = buffer.getReadPointer (0);
    const float* right = buffer.getReadPointer (channels > 1 ? 1 : 0);
    float peak = 0.0f;
    /* Held for the block; null while no bus is claimed, which push ignores. */
    auto* pusher = static_cast<abus_pusher_t*> (shell_handoff_acquire (handoff));
    ni::wire::for_each_chunk (frames, stageFrames, [&] (int off, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = left[off + i], r = right[off + i];
            stage[(std::size_t) i * 2] = l;
            stage[(std::size_t) i * 2 + 1] = r;
            peak = std::max (peak, std::max (std::fabs (l), std::fabs (r)));
        }
        /* Stamped with where the chunk sits on the host's timeline, so a
         * reader on another track can line it up with its own blocks. */
        abus_pusher_push_at (pusher, stage.data(), (std::uint32_t) n, clock.timeInSamples + off, clock.timed ? 1 : 0);
    });
    shell_handoff_release (handoff);

    /* Clamped here, so every reader gets the same answer: a sample above full
     * scale is real, a meter drawn past its well is not. */
    const float prev = level.load (std::memory_order_relaxed);
    level.store (std::min (1.0f, std::max (peak, prev * peakDecay)), std::memory_order_relaxed);
}

/* Bypassed, by the host or by the Bypass -- audio through, nothing
 * published -- while the meter falls and the Ground keeps the host's time. */
void Processor::processBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    beat.tick (readClock (getPlayHead()), buffer.getNumSamples());
    fall();
    ni::Processor::processBypassed (buffer, midi);
}

void Processor::fall() noexcept
{
    level.store (level.load (std::memory_order_relaxed) * peakDecay, std::memory_order_relaxed);
}

/*
 * THE BUS FOLLOWS WHAT THE OTHER THREADS ASKED FOR, here and nowhere else.
 * Nothing is claimed before the host's first prepare; a prepare retunes a live
 * bus and retries a refused one; a new bus number or a loaded state claims
 * afresh.
 */
void Processor::serviceBus()
{
    /* A pusher replaced earlier is freed once the audio thread has let go. */
    shell_handoff_collect (handoff);
    serviceLabel();

    const std::uint32_t r = rate.load (std::memory_order_acquire);
    if (r == 0)
        return;

    const bool reset = resetSeen.exchange (false, std::memory_order_acq_rel);
    const bool reload = reclaim;
    reclaim = false;
    const int want = wire::clamp_slot ((int) std::lround (bus->plain()));

    /* Either side of a rate change is a different signal, so the bus restarts
     * rather than splicing. Posted: the pusher applies it. */
    if (reset && writer != nullptr)
        abus_writer_set_sample_rate (writer, r);

    if (! (waiting || reload || want != triedSlot || (reset && writer == nullptr)))
        return;

    /*
     * RELEASE FIRST, AND ONLY THEN CLAIM: moving 3 -> 4 -> 3 would otherwise
     * find bus 3 still held by this instance and call it taken. The writer
     * goes at once; the bus goes with the pusher once the audio thread lets go
     * -- within a block -- and until then the claim waits for the next tick.
     */
    abus_writer_release (writer);
    writer = nullptr;
    shell_handoff_set (handoff, nullptr);
    waiting = shell_handoff_collect (handoff) > 0;
    if (waiting)
        return;

    triedSlot = want;
    abus_writer_t* w = nullptr;
    abus_pusher_t* p = nullptr;
    switch (abus_writer_claim ((std::uint32_t) want, r, &w, &p))
    {
        case ABUS_OK:
            state = Status::live;
            writer = w;
            if (! named.empty())
                abus_writer_set_label (writer, named.c_str());
            shell_handoff_set (handoff, p);
            break;
        case ABUS_ERR_TAKEN:
            /* Another Listen-In publishes here, and the window says so: a tap
             * that silently does nothing is worse than one that refuses out
             * loud. */
            state = Status::taken;
            break;
        default:
            state = Status::unavailable;
            break;
    }
}

void Processor::serviceLabel()
{
    bool loaded = false;
    if (! session.take (named, loaded))
        return;
    if (writer != nullptr)
        abus_writer_set_label (writer, named.c_str());
    /* The bus and its name changed underneath the claim: claim afresh. */
    if (loaded)
        reclaim = true;
}

void Processor::editLabel (const std::string& typed)
{
    JUCE_ASSERT_MESSAGE_THREAD
    auto name = kept (typed.c_str());
    if (name == session.label())
        return;
    session.edit (std::move (name));
    serviceLabel();
    /* A name is not a parameter, so the host cannot see it change. */
    nonParameterStateChanged();
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new ni::PluginEditor (*this, std::make_unique<ListenInEditor> (*editorModel),
                                 ni::PluginEditor::FollowDesign {});
}

/* While a window shows it: the Ground. */
void Processor::editorOpened()
{
    beat.setActive (true);
}

void Processor::editorClosed()
{
    beat.setActive (false);
}

/* THE CHUNK THE iPlug2 BUILD WROTE: the bus, its name, and the bypass after
 * it, which iPlug2 needs to load it. */
void Processor::writeState (juce::MemoryBlock& out)
{
    nist::State saved;
    saved.params.push_back (bus->plain());
    saved.strings.push_back (session.label());
    saved.bypass = bypass->plain() >= 0.5;
    const auto bytes = nist::write (layout(), saved);
    out.replaceAll (bytes.data(), bytes.size());
}

/*
 * A SET REOPENED: every check before anything is applied, so a chunk no build
 * wrote changes nothing; then the bus, the bypass and the name, kept as the
 * editor's would be. The bus itself is claimed afresh by the message thread.
 */
bool Processor::readState (const void* data, size_t size)
{
    const auto loaded = nist::read (layout(), data, size);
    if (! loaded)
        return false;
    bus->setPlainNotifyingHost (loaded->params[kBus]);
    if (loaded->bypass)
        bypass->setPlainNotifyingHost (*loaded->bypass ? 1.0 : 0.0);
    session.load (kept (loaded->strings.front().c_str()));
    return true;
}

} // namespace ni::li

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::li::Processor();
}

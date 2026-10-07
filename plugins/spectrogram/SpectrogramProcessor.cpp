// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Spectrogram on the JUCE shell. SpectrogramProcessor.h says what it keeps
 * and where.
 */
#include "SpectrogramProcessor.h"

#include "EngineModel.h"
#include "PluginEditor.h"
#include "SpectrogramEditor.h"
#include "ni/Wire.h"

#include <cmath>

namespace ni::spectrogram
{

namespace
{
/* The handoff frees through this; srecv_free's pointer type is not void*. */
void freeReceiver (void* r)
{
    srecv_free (static_cast<srecv_t*> (r));
}

srecv_t* newReceiver (float sampleRate)
{
    const int fftSize = spectro_pick_fft_size (sampleRate);
    return srecv_new (sampleRate, fftSize, spectro_pick_hop (sampleRate, fftSize), SPECTRO_BANDS, SPECTRO_F_MIN,
                      SPECTRO_F_MAX, SPECTRO_DB_FLOOR, SPECTRO_DB_CEIL);
}
} // namespace

Processor::Processor()
    : ni::Processor (BusesProperties()
                         .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      lend (shell_handoff_new (freeReceiver))
{
    /* The one parameter, so its index -- and VST3 ID -- is 0. */
    auto b = std::make_unique<ni::Parameter> (ni::bypassSpec());
    bypass = b.get();
    addParameter (b.release());

    /* Configured for real once the host has named a rate (prepareToPlay);
     * this one lets a window opened before that draw an axis. */
    recv = newReceiver (48000.0f);
    if (recv != nullptr)
        srecv_start (recv);
    shell_handoff_set (lend.get(), recv);

    editorModel = std::make_unique<EngineModel> (*this);
    startTimerHz (serviceHz);
}

/* The audio thread has stopped; the handoff frees the receiver, joining its
 * worker, after the model that reads it has gone. */
Processor::~Processor()
{
    stopTimer();
    editorModel.reset();
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

/*
 * THE HOST'S SAMPLE RATE REACHES THE FREQUENCY AXIS THROUGH HERE: a band's
 * bin range depends on it. The receiver is REPLACED rather than reconfigured
 * -- every buffer's size depends on the configuration -- and that allocates
 * and starts a thread, so it happens in service(); this may not be the
 * message thread.
 */
void Processor::prepareToPlay (double rate, int maximumBlock)
{
    sampleRate = rate > 0.0 ? rate : 44100.0;
    const auto sr = (float) sampleRate;
    /* The hop ties the picture to a column RATE, so it holds the same seconds
     * at any sample rate; ppqPerColumn is hop over rate. */
    hop.store (spectro_pick_hop (sr, spectro_pick_fft_size (sr)), std::memory_order_relaxed);
    pubRate.store ((int) std::lround (sampleRate), std::memory_order_relaxed);
    mono.assign ((std::size_t) juce::jmax (maximumBlock, 1), 0.0f);
    beat.prepare (sampleRate);
    recvRate.store (sr, std::memory_order_relaxed);
    stale.store (true, std::memory_order_release);
}

/*
 * ONE BLOCK: the Ground and the host's clock, then the mono sum into the
 * receiver's ring and nothing else -- the transforms run on its worker, with
 * every other source, in step. The audio is not touched: JUCE hands the
 * block in place, so what came in goes out bit for bit.
 */
void Processor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int frames = buffer.getNumSamples();
    const auto clock = readClock (getPlayHead());
    beat.tick (clock, frames);

    /*
     * THE HOST'S CLOCK, and the ways a host leaves it out: no tempo (0), and
     * a running transport with no musical position (NaN). Running, the
     * host's position is the authority -- alignment is what the bar view is
     * for; otherwise the position advances at the last tempo, so the picture
     * keeps filling.
     */
    if (clock.bpm > 1.0 && clock.bpm < 1000.0)
        lastBpm = clock.bpm;
    const bool running = clock.known && clock.playing && std::isfinite (clock.ppq) && clock.ppq >= 0.0;
    position = running ? clock.ppq : ni::wire::advance_beats (position, frames, lastBpm, sampleRate);
    const int h = hop.load (std::memory_order_relaxed);
    pubPpq.store (position, std::memory_order_relaxed);
    pubBpm.store (lastBpm, std::memory_order_relaxed);
    pubRunning.store (running, std::memory_order_relaxed);
    pubPpqPerColumn.store (h > 0 && sampleRate > 0.0 ? (double) h / sampleRate * (lastBpm / 60.0) : 0.0,
                           std::memory_order_relaxed);
    const int num = clock.numerator > 0 ? clock.numerator : 4;
    const int den = clock.denominator > 0 ? clock.denominator : 4;
    pubMeter.store ((num & 0xFF) << 8 | (den & 0xFF), std::memory_order_relaxed);

    const int cap = (int) mono.size();
    if (frames <= 0 || buffer.getNumChannels() < 1 || cap <= 0)
        return;
    /* Held for the block; nothing is fed while a rebuild for a new rate is
     * pending, since the receiver in hand was built for the old one. */
    auto* r = static_cast<srecv_t*> (shell_handoff_acquire (lend.get()));
    if (r != nullptr && ! stale.load (std::memory_order_acquire))
    {
        const float* left = buffer.getReadPointer (0);
        const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : left;
        ni::wire::for_each_chunk (frames, cap, [&] (int off, int n)
        {
            /* Halved: a centred mix summed without it reads 6 dB hot. */
            for (int i = 0; i < n; ++i)
                mono[(std::size_t) i] = 0.5f * (left[off + i] + right[off + i]);
            srecv_push_own (r, mono.data(), n);
        });
    }
    shell_handoff_release (lend.get());
}

/* Bypassed, the host's own way -- audio through, nothing analysed -- while
 * the Ground keeps the host's time, so the window's beat does not stop. */
void Processor::processBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    beat.tick (readClock (getPlayHead()), buffer.getNumSamples());
    ni::Processor::processBypassed (buffer, midi);
}

Transport Processor::transport() const noexcept
{
    Transport t;
    t.ppq = pubPpq.load (std::memory_order_relaxed);
    t.bpm = pubBpm.load (std::memory_order_relaxed);
    const int meter = pubMeter.load (std::memory_order_relaxed);
    t.numerator = (meter >> 8) & 0xFF;
    t.denominator = meter & 0xFF;
    t.running = pubRunning.load (std::memory_order_relaxed);
    t.ppqPerColumn = pubPpqPerColumn.load (std::memory_order_relaxed);
    t.sampleRate = pubRate.load (std::memory_order_relaxed);
    return t;
}

/* --------------------------------------------------------- the receiver -- */

void Processor::service()
{
    shell_handoff_collect (lend.get());
    if (stale.load (std::memory_order_acquire))
        rebuild();
    else
        look.service (*this);
    if (recv == nullptr)
        return;
    /* The transforms, here, only if the worker could not be started. */
    srecv_pump (recv);
    if (showing)
        editorModel->readSourcesIfDue (juce::Time::getMillisecondCounterHiRes());
    else
        srecv_frame (recv, nullptr, 0, -1, -1, nullptr, nullptr, Model::maxColumns, nullptr);
}

/*
 * THE RECEIVER IS REBUILT WHERE IT IS DRAINED. The window is the engine's
 * pick for the rate, and the session survives the rebuild: the buses are
 * reopened against the new rate, which is also where one that does not match
 * it starts being refused.
 */
void Processor::rebuild()
{
    const float sr = recvRate.load (std::memory_order_relaxed);
    srecv_t* fresh = newReceiver (sr);
    if (fresh == nullptr)
        return;

    /* The old one is freed -- its worker joined -- once the audio thread has
     * let go of it. */
    shell_handoff_set (lend.get(), fresh);
    recv = fresh;
    /* Everything, to the new receiver: its sources, its clash, its range and
     * the axis that range makes. */
    look.service (*this, true);
    /* Started once its sources are open, so choosing them waits for no
     * thread. */
    srecv_start (fresh);
    /* Cleared last: the audio thread feeds the new receiver only now --
     * unless prepareToPlay asked again meanwhile, and then the next tick
     * rebuilds. */
    if (juce::exactlyEqual (recvRate.load (std::memory_order_relaxed), sr))
        stale.store (false, std::memory_order_release);
    shell_handoff_collect (lend.get());
    editorModel->readAxis();
}

void Processor::sessionEdited()
{
    look.service (*this);
    nonParameterStateChanged();
}

/* Message thread only: it opens and closes readers, which allocates and maps
 * memory, and waits for the analysis thread to adopt the change. */
void Processor::applySources (const std::vector<unsigned int>& slots)
{
    if (recv != nullptr)
        srecv_set_sources (recv, slots.empty() ? nullptr : slots.data(), (int) slots.size());
}

void Processor::applyClash (float floorDb, float balanceDb)
{
    if (recv != nullptr)
        srecv_set_clash (recv, floorDb, balanceDb);
}

/* The engine refuses an undrawable range and clamps the top to Nyquist, so
 * the axis is read back as the engine has it. */
void Processor::applyRange (float lo, float hi)
{
    if (recv == nullptr)
        return;
    srecv_set_range (recv, lo, hi);
    if (editorModel != nullptr)
        editorModel->readAxis();
}

/* ------------------------------------------------------------- the editor -- */

juce::AudioProcessorEditor* Processor::createEditor()
{
    auto design = std::make_unique<SpectrogramEditor> (*editorModel);
    design->setTitle ("NI Spectrogram");
    return new ni::PluginEditor (*this, std::move (design), SpectrogramEditor::designWidth,
                                 SpectrogramEditor::designHeight);
}

/* While a window shows them: the columns, the bus list and the Ground. The
 * picker's contents now, not when the slow look comes round. */
void Processor::editorOpened()
{
    showing = true;
    beat.setActive (true);
    editorModel->readSources();
}

void Processor::editorClosed()
{
    showing = false;
    beat.setActive (false);
}

/* ---------------------------------------------------------------- state -- */

/*
 * THE CHUNK THE iPlug2 BUILD WROTE: what the session holds -- a load the
 * message thread has not applied yet included -- and the bypass after it,
 * which iPlug2 needs to load it. Neither call touches the receiver, because
 * a host picks the thread (State.h).
 */
void Processor::writeState (juce::MemoryBlock& out)
{
    const auto bytes = spectro::state::write (look.get(), bypass->plain() >= 0.5);
    out.replaceAll (bytes.data(), bytes.size());
}

bool Processor::readState (const void* data, size_t size)
{
    std::optional<bool> flag;
    if (! look.load (data, size, flag))
        return false;
    if (flag)
        bypass->setPlainNotifyingHost (*flag ? 1.0 : 0.0);
    return true;
}

} // namespace ni::spectrogram

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::spectrogram::Processor();
}

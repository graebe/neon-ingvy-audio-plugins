// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Spectrogram on the JUCE shell: the iPlug2 build's identity and saved
 * session, around the same Rust analyzer, with the native editor.
 *
 * WHAT CARRIES A LIVE SET ACROSS FROM THE iPlug2 BUILD, each in its place:
 *
 *   the class        the iPlug2 class ID itself (CMakeLists.txt: IPLUG2_CLASS,
 *                    JUCE_VST3_COMPONENT_CLASS), so a set finds this build as
 *                    it found the old one
 *   the bundle       NISpectrogram.vst3, the old bundle's name, so installing
 *                    this one replaces it instead of standing beside it with
 *                    the same class
 *   the parameters   none but the host's Bypass: ID 0 here, 65536 there, which
 *                    Live maps by its flag
 *   the state        the iPlug2 chunk, read in every layout it ever had and
 *                    written as the last one wrote it (State.h); the bypass
 *                    it carries is restored too, which iPlug2 left in its
 *                    controller
 *
 * AUDIO PASSES THROUGH BIT FOR BIT; the plugin's whole output is a picture.
 * The analysis is the Rust receiver's (spectro_recv.h): the FFT, the log
 * bands, the dB scale, the sum of several sources and their clash.
 *
 * WHAT CROSSES A THREAD. process() copies the mono sum into the receiver's
 * ring, ticks the Ground and publishes the host's clock, and nothing else; the
 * receiver's own worker runs the transforms and leaves finished columns in
 * lock-free rings, which the editor's model drains on the message thread
 * straight into the picture's buffers (EngineModel). The host's state calls,
 * on whatever thread it makes them, only record what the session is looking
 * at (State.h's Session); the message thread applies it to the receiver.
 *
 * THE RECEIVER IS THE MESSAGE THREAD'S: built, configured, drained and freed
 * there, and freeing it joins its worker. The audio thread reaches it only
 * through a handoff (shell_handoff.h), which frees a replaced one once the
 * audio thread has let go. prepareToPlay -- which a host may call off the
 * message thread -- records the rate and asks for a rebuild; until service()
 * has made it, the audio thread feeds nothing rather than a receiver built
 * for the old rate.
 */
#pragma once

#include "GroundClock.h"
#include "Parameter.h"
#include "Processor.h"
#include "State.h"
#include "Model.h"
#include "shell_handoff.h"
#include "spectro_recv.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace ni::spectrogram
{

class EngineModel;

class Processor final : public ni::Processor,
                        private juce::Timer,
                        private spectro::state::Session::Sink
{
public:
    /* The receiver's service: a host's idle rate, as iPlug2's was. */
    static constexpr int serviceHz = 50;

    Processor();
    ~Processor() override;

    /* Stereo in, stereo out, and nothing else: the iPlug2 build's "2-2". */
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    void editorOpened() override;
    void editorClosed() override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypass; }

    /* ---- what the model and the tests reach. Message thread, unless said. */

    /*
     * One tick of the receiver's life, whether or not a window is open: the
     * rebuild a new rate asked for, a load or an edit applied, a pump should
     * the worker not run, and -- with no window open -- the finished columns
     * dropped, so the rings do not fill with a picture nobody will see. What
     * the timer does at serviceHz.
     */
    void service();

    /* The receiver now; null only if one could not be built. */
    srecv_t* receiver() const noexcept { return recv; }
    /* Whether a receiver for the host's rate is still to be built. Any thread. */
    bool rebuildPending() const noexcept { return stale.load (std::memory_order_acquire); }

    spectro::state::Session& session() noexcept { return look; }
    /* The session changed through the editor: the receiver gets what moved,
     * and the set is marked unsaved -- none of it is a host parameter. */
    void sessionEdited();

    /* The host's clock as the audio thread last published it. Any thread. */
    Transport transport() const noexcept;

    ni::Parameter& bypassParameter() const noexcept { return *bypass; }
    ni::GroundClock& ground() noexcept { return beat; }
    EngineModel& model() noexcept { return *editorModel; }
    bool editorOpen() const noexcept { return showing; }

protected:
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void writeState (juce::MemoryBlock&) override;
    bool readState (const void* data, size_t size) override;

private:
    void timerCallback() override { service(); }

    /* The receiver, built for the rate prepareToPlay recorded. */
    void rebuild();

    /* Session::Sink: the receiver calls, message thread only (they allocate,
     * and choosing sources waits for the analysis thread). */
    void applySources (const std::vector<unsigned int>& slots) override;
    void applyClash (float floorDb, float balanceDb) override;
    void applyRange (float lo, float hi) override;

    ni::Parameter* bypass = nullptr;

    spectro::state::Session look;

    srecv_t* recv = nullptr;
    struct HandoffDeleter
    {
        void operator() (shell_handoff_t* h) const noexcept { shell_handoff_free (h); }
    };
    std::unique_ptr<shell_handoff_t, HandoffDeleter> lend;
    std::atomic<float> recvRate { 0.0f };
    std::atomic<bool> stale { false };

    /* The mono sum, sized in prepareToPlay; a longer block goes in chunks. */
    std::vector<float> mono;
    /* The analyzer's hop at the host's rate, for ppqPerColumn. */
    std::atomic<int> hop { 0 };
    double sampleRate = 0.0;

    /*
     * THE HOST'S CLOCK, written on the audio thread -- the only place a
     * host's transport is legible -- and read by the model. Relaxed: separate
     * facts for a picture, where a tick mixing two blocks is off by one frame
     * of drawing.
     */
    static_assert (std::atomic<double>::is_always_lock_free,
                   "publishing a playhead must not take a lock on the audio thread");
    std::atomic<double> pubPpq { 0.0 }, pubBpm { 120.0 }, pubPpqPerColumn { 0.0 };
    std::atomic<int> pubMeter { (4 << 8) | 4 }; /* numerator << 8 | denominator */
    std::atomic<bool> pubRunning { false };
    std::atomic<int> pubRate { 0 };
    /* The audio thread's position: the host's while it runs, advancing at the
     * last tempo seen while it does not, so the picture keeps filling. */
    double position = 0.0, lastBpm = 120.0;

    ni::GroundClock beat;
    bool showing = false;

    std::unique_ptr<EngineModel> editorModel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace ni::spectrogram

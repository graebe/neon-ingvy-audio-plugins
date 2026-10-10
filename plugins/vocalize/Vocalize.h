// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize: any sound, more and more a voice singing A, E, I, O or U.
 *
 * THE ENGINE IS RUST (engines/vocalize): the vowel, both engines, the
 * parameter table and its text. This file is the shell around it, and does
 * what a shell does:
 *
 *   parameters    declared from the engine's table (vz_param_*): seven
 *                 numbers printed and read in the engine's words, and Mode, a
 *                 choice of its labels that a host may not automate. The IDs
 *                 are the engine's keys, the state the default ni::Processor
 *                 one (every parameter by its ID)
 *   the buses     mono in to mono out, or stereo to stereo; no MIDI, no
 *                 sidechain
 *   the latency   Mode's (vz_latency): none for Time, one frame for Spectral.
 *                 Set in prepareToPlay, and on the message thread by a timer
 *                 that compares it with Mode's: a host may write Mode on the
 *                 audio thread, and nothing there may post a message (an
 *                 AsyncUpdater's post allocates)
 *   a block       the shell taken (vz_shell_begin), every parameter pushed
 *                 (cheap when unchanged), the channels processed in place, the
 *                 shell given back -- which publishes the picture when one is
 *                 due
 *   activation    prepareToPlay posts the rate and a reset, so a host that
 *                 stopped and started the plugin hears nothing it held before
 *   the editor    reads the engine's picture and the vowel's formants through
 *                 the Model, on the message thread; the Ground's clock runs
 *                 while a window is open
 *
 * A VOICE OF ONE'S OWN. capture() reads a recording -- WAV, AIFF, and on
 * macOS whatever the system decodes -- on a thread of its own, downmixes it,
 * and has the engine's analysis (vz_voice_capture) find the singer's vowels;
 * the answer comes back on the message thread, goes to the engine through the
 * shell (vz_shell_post_voice) and marks the set unsaved. The voice is kept in
 * the state as fifty numbers, never the audio, and checked on the way back in
 * (vz_voice_check). Voice, a parameter, says how much of it is sung.
 *
 * THE PANIC. On the Move, CC 120/123 reset the engine (vz-move); a VST3 in
 * Live is told to start afresh by being deactivated, which prepareToPlay
 * answers with the same reset.
 *
 * The audio thread allocates nothing: the engine does not, and nothing here
 * does on its way there (tests/vocalize/vz_rt.cpp holds it to that).
 */
#pragma once

#include "GroundClock.h"
#include "Model.h"
#include "Processor.h"
#include "vz_shell.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace ni::vz
{

class Processor final : public ni::Processor,
                        public Model,
                        private juce::Timer
{
public:
    static constexpr int paramCount = param::count;

    Processor();
    ~Processor() override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    void editorOpened() override;
    void editorClosed() override;

    /* A parameter's value in its own unit, as the engine takes it. Any
     * thread. */
    double plain (int index) const;

    /* The latency Mode asks for now, at the rate last prepared: what
     * getLatencySamples() becomes once the message thread has caught up. */
    int latencyForMode() const;

    /* ---- the Model, for the editor: message thread */
    int numParameters() const override { return paramCount; }
    juce::RangedAudioParameter& parameter (int index) override { return *params[(size_t) index]; }
    int takeRings (float* strengths, int capacity) override;
    bool motion() const override;
    void setMotion (bool on) override;
    bool bipolar (int index) const override;
    Formants formants (double vowel) override;
    Voice voice() const override;
    void capture (const juce::File& recording) override;
    void forgetVoice() override;
    juce::String recordingWildcard() const override;
    const Picture& picture() override;

    /* The longest part of a recording a capture reads, in seconds. */
    static constexpr double longestRecordingS = 120.0;

protected:
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    /* The captured voice beside the parameters, as <voice>; none, none. */
    void writeExtraState (juce::XmlElement&) override;
    void readExtraState (const juce::XmlElement&) override;
    /* Bypassed, the host's audio passes untouched but as late as the
     * latency the host compensates for -- JUCE's own bypass does not delay
     * -- and the Ground keeps the host's time. */
    void processBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    /* How often the message thread checks the latency against Mode's. */
    static constexpr int latencyCheckMs = 50;
    /* The bypass delay's length: a power of two above the longest latency
     * (vz_latency's 4096 at 192 kHz). */
    static constexpr int bypassLength = 8192;

    void timerCallback() override;

    /* A voice in hand: kept, sent to the engine, the set marked unsaved when
     * it is the person's doing (not a state being read). Null forgets. */
    void useVoice (const VzVoice*, const juce::String& name, bool byHand);
    /* What a capture on its thread found, back on the message thread. */
    void captured (std::optional<VzVoice>, Voice::Status, const juce::String& name);

    struct ShellDeleter
    {
        void operator() (VzShell* s) const { vz_shell_destroy (s); }
    };
    std::unique_ptr<VzShell, ShellDeleter> shell;
    std::array<juce::RangedAudioParameter*, paramCount> params {};
    ni::GroundClock beat;
    std::atomic<double> rate { 44100.0 };

    /* The bypassed signal, delayed: made here, emptied by prepareToPlay. */
    std::array<std::vector<float>, 2> bypassLine;
    int bypassPos = 0;

    /* THE CAPTURED VOICE, and what the editor is told of it. Read and
     * written by the message thread and by a host's state calls, which may
     * come on another: under the lock, never taken by the audio thread. */
    mutable juce::CriticalSection voiceLock;
    std::optional<VzVoice> held;
    Voice state;
    juce::AudioFormatManager formats;
    /* The capture's one thread; waited for when the processor goes. */
    juce::ThreadPool captures { juce::ThreadPoolOptions{}.withThreadName ("NI Vocalize capture").withNumberOfThreads (1) };

    /* The picture as the engine published it, and as the editor takes it. */
    std::unique_ptr<VzPicture> published;
    Picture latest;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Processor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace ni::vz

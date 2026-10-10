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
 *                 Set in prepareToPlay, and again on the message thread
 *                 whenever Mode changes -- the parameter's listener only asks
 *                 for that (an AsyncUpdater), since it may be called on the
 *                 audio thread
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

#include <array>
#include <atomic>
#include <memory>

namespace ni::vz
{

class Processor final : public ni::Processor,
                        public Model,
                        private juce::AudioProcessorParameter::Listener,
                        private juce::AsyncUpdater
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
    const Picture& picture() override;

protected:
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    /* Bypassed, the host's audio passes untouched -- delayed by the latency,
     * which JUCE's own bypass keeps -- and the Ground keeps the host's time. */
    void processBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    void parameterValueChanged (int index, float) override;
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override;

    struct ShellDeleter
    {
        void operator() (VzShell* s) const { vz_shell_destroy (s); }
    };
    std::unique_ptr<VzShell, ShellDeleter> shell;
    std::array<juce::RangedAudioParameter*, paramCount> params {};
    ni::GroundClock beat;
    std::atomic<double> rate { 44100.0 };

    /* The picture as the engine published it, and as the editor takes it. */
    std::unique_ptr<VzPicture> published;
    Picture latest;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace ni::vz

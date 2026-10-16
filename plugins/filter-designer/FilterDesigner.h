// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Filter Designer: a filter design tool you play as an equaliser.
 *
 * THE ENGINE IS RUST (engines/filter-designer): the sixteen nodes, the
 * isolator, the parameter table and its text, and the picture the editor
 * draws. This file is the shell around it, and does what a shell does:
 *
 *   parameters    declared from the engine's table (fd_param_*): 175 of them,
 *                 numbers printed and read in the engine's words, choices of
 *                 its labels. The IDs are the engine's keys, the state the
 *                 default ni::Processor one (every parameter by its ID)
 *   the bypass    the shared Bypass (ni::bypassSpec), the host's own: the
 *                 shell crossfades it over 10 ms (ni::Processor), and while
 *                 bypassed the engine keeps running on a copy of the input,
 *                 so its filters' state is current when the bypass is lifted
 *   the buses     mono in to mono out, or stereo to stereo; no MIDI, no
 *                 sidechain; no latency, ever (minimum phase)
 *   the tail      the engine's (fd_tail_seconds): how long the filters the
 *                 host's values make ring on after the input stops, to
 *                 -60 dB, at most 30 s; asked for when the host sets up
 *   a block       the shell taken (fd_shell_begin), every parameter pushed
 *                 (cheap when unchanged), the channels processed in place, the
 *                 shell given back -- which publishes the analyzer's rings
 *                 when one is due
 *   activation    prepareToPlay posts the rate and a reset, so a host that
 *                 stopped and started the plugin hears nothing it held before
 *   the editor    reads the picture through the Model, on the message thread:
 *                 fd_shell_read designs it from the values the host holds,
 *                 so what is drawn is what plays; the analyzer's rings are
 *                 kept only while a window is open
 *
 * THE PANIC. On the Move, CC 120/123 reset the engine (fd-move); a VST3 in
 * Live is told to start afresh by being deactivated, which prepareToPlay
 * answers with the same reset.
 *
 * The audio thread allocates nothing: the engine does not, and nothing here
 * does on its way there (tests/filter-designer/fd_rt.cpp holds it to that).
 */
#pragma once

#include "Model.h"
#include "Parameter.h"
#include "Processor.h"
#include "fd_shell.h"

#include <array>
#include <atomic>
#include <memory>

namespace ni::fd
{

class Processor final : public ni::Processor,
                        public Model
{
public:
    static constexpr int paramCount = param::count;

    Processor();
    ~Processor() override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void releaseResources() override {}

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypass; }
    double getTailLengthSeconds() const override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    void editorOpened() override;
    void editorClosed() override;

    /* A parameter's value in its own unit, as the engine takes it. Any
     * thread. */
    double plain (int index) const;

    /* ---- the Model, for the editor: message thread */
    int numParameters() const override { return paramCount; }
    juce::RangedAudioParameter& parameter (int index) override { return *params[(size_t) index]; }
    juce::RangedAudioParameter& bypassParameter() override { return *bypass; }
    bool bipolar (int index) const override;
    const Picture& picture (int lane) override;
    unsigned typeUses (int type, int order) const override;
    int orderPlayed (int type, int family, int order) const override;
    bool familyUsesRipple (int family) const override;
    bool familyUsesStopband (int family) const override;
    double qForEdge (double freq, double edgeHz) const override;
    int typeForNew (double hz) const override;

protected:
    /* The shell's prepareToPlay calls this first (ni::Processor). */
    void prepare (double sampleRate, int maximumBlockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    /* Bypassed: JUCE's own pass-through, the engine running on a copy. */
    void processBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    /* One block through the engine: in place when `heard`, on a copy of the
     * input when bypassed, so the engine's state follows the music. */
    void run (juce::AudioBuffer<float>&, bool heard);
    /* The analyzer's rings are kept while a window shows them: posted to the
     * engine when that changes. Message thread. */
    void watch();

    struct ShellDeleter
    {
        void operator() (FdShell* s) const { fd_shell_destroy (s); }
    };
    std::unique_ptr<FdShell, ShellDeleter> shell;
    std::array<juce::RangedAudioParameter*, paramCount> params {};
    ni::Parameter* bypass = nullptr;
    /* The bypassed input's copy, sized in prepare. */
    std::array<std::vector<float>, 2> listening;
    /* The rate prepare was given, for the tail; 0 before. */
    std::atomic<double> rate { 0.0 };
    bool windowOpen = false, watching = false;

    /* The picture as fd_shell_read writes it, and as the editor takes it. */
    std::unique_ptr<FdPicture> published;
    Picture latest;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace ni::fd

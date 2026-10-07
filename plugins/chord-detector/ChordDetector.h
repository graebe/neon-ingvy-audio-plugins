// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Chord-Detector: a silent instrument that names what its MIDI lane plays.
 *
 * AN INSTRUMENT WITH NO SOUND, because that is how Live hands a VST3 the
 * notes of a clip: Live hosts no VST3 MIDI effect. It sits in an Instrument
 * Rack chain beside the synth, or on a track that takes its MIDI from
 * another; its stereo output is silence.
 *
 * THE ENGINE IS RUST (engines/chord-detector): the notes sounding, their
 * name, the history's clock, the parameter table. This file is the shell
 * around it, and does what a shell does:
 *
 *   parameters    declared from the engine's table (cd_param_*): seven
 *                 choices -- Key, Mode, Spelling, Hold, and the window's
 *                 History, Span and Zoom, which a host saves and does not
 *                 automate
 *   a block       the shell taken (cd_shell_begin), the host's clock, every
 *                 parameter (cheap when unchanged), every MIDI message at its
 *                 sample offset, the end of the block, the shell given back
 *   the editor    reads the engine's latest reading and drains its note
 *                 events through the Model, on the message thread
 *
 * The audio thread allocates nothing: the engine does not, and nothing here
 * does on its way there.
 */
#pragma once

#include "Model.h"
#include "Processor.h"
#include "cd_capi.h"

#include <array>
#include <atomic>
#include <memory>

namespace ni::chord_detector
{

class ChordDetector final : public ni::Processor,
                            public Model
{
public:
    ChordDetector();
    ~ChordDetector() override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    /* ---- the Model, for the editor: message thread */
    int numParameters() const override { return paramCount; }
    juce::RangedAudioParameter& parameter (int index) override { return *params[(size_t) index]; }
    const CdReading& reading() override;
    int takeNotes (CdNoteEvent* out, int capacity) override;
    CdWrittenNote write (int midi) const override;

    /* The scales the Zoom choices stand for. */
    static const std::vector<float>& zoomScales();

protected:
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    int choice (int index) const;

    struct ShellDeleter
    {
        void operator() (CdShell* s) const { cd_shell_destroy (s); }
    };
    std::unique_ptr<CdShell, ShellDeleter> shell;
    std::array<juce::AudioParameterChoice*, paramCount> params {};
    CdReading latest {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordDetector)
};

} // namespace ni::chord_detector

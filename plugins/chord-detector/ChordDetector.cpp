// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Chord-Detector's shell. ChordDetector.h says what it does.
 */
#include "ChordDetector.h"

#include "Editor.h"
#include "PluginEditor.h"

namespace ni::chord_detector
{

namespace
{
/* One of the engine's texts for a parameter: cd_param_key, _name or _choice. */
template <typename Read>
juce::String engineText (Read read)
{
    char buf[64] {};
    return read (buf, sizeof buf) >= 0 ? juce::String::fromUTF8 (buf) : juce::String();
}

std::unique_ptr<juce::AudioParameterChoice> choiceFromEngine (int index)
{
    CdParamInfo info {};
    const bool known = cd_param_info (index, &info);
    jassert (known);
    juce::ignoreUnused (known);

    juce::StringArray choices;
    for (int c = 0; c < info.choice_count; ++c)
        choices.add (engineText ([&] (char* b, size_t n) { return cd_param_choice (index, c, b, n); }));

    const auto key = engineText ([&] (char* b, size_t n) { return cd_param_key (index, b, n); });
    const auto name = engineText ([&] (char* b, size_t n) { return cd_param_name (index, b, n); });
    return std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { key, 1 }, name, choices, info.default_choice,
        juce::AudioParameterChoiceAttributes().withAutomatable (info.automatable));
}
} // namespace

ChordDetector::ChordDetector()
    : ni::Processor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      shell (cd_shell_create (44100.0))
{
    jassert (cd_param_count() == paramCount);
    for (int i = 0; i < paramCount; ++i)
    {
        auto p = choiceFromEngine (i);
        params[(size_t) i] = p.get();
        addParameter (p.release());
    }
}

ChordDetector::~ChordDetector() = default;

const std::vector<float>& ChordDetector::zoomScales()
{
    static const std::vector<float> scales { 0.75f, 1.0f, 1.25f, 1.5f };
    return scales;
}

bool ChordDetector::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto out = layout.getMainOutputChannelSet();
    return layout.inputBuses.isEmpty() && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void ChordDetector::prepareToPlay (double sampleRate, int)
{
    /* A host that deactivated the plugin sent no note-offs for what was held
     * then, so it starts again from silence. */
    cd_shell_post_reset (shell.get());
    cd_shell_post_sample_rate (shell.get(), sampleRate);
}

int ChordDetector::choice (int index) const
{
    return params[(size_t) index]->getIndex();
}

void ChordDetector::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const auto frames = (uint32_t) buffer.getNumSamples();
    const auto clock = ni::readClock (getPlayHead());

    CdCore* core = cd_shell_begin (shell.get());
    cd_core_begin_block (core, clock.known ? 1 : 0, clock.ppq, clock.bpm, clock.numerator, clock.denominator,
                         clock.playing ? 1 : 0);
    for (int i = 0; i < paramCount; ++i)
        cd_core_set_param (core, i, choice (i));
    for (const auto m : midi)
    {
        const auto offset = (uint32_t) juce::jlimit (0, (int) frames, m.samplePosition);
        cd_core_on_midi (core, m.data, (size_t) m.numBytes, offset);
    }
    cd_core_end_block (core, frames);
    cd_shell_end (shell.get(), frames);

    /* It names; it does not sound. */
    buffer.clear();
}

namespace
{
/* The engine's 128-bit note set: note n at bit n % 64 of word n / 64. */
ni::ui::music::NoteSet noteSet (const uint64_t (&words)[2])
{
    ni::ui::music::NoteSet set;
    for (int n = 0; n < 128; ++n)
        set[(size_t) n] = ((words[n / 64] >> (n % 64)) & 1u) != 0;
    return set;
}
} // namespace

const Reading& ChordDetector::reading()
{
    CdReading r {};
    cd_shell_read (shell.get(), &r);
    latest.now = r.now;
    latest.bpm = r.bpm;
    latest.bar = r.bar;
    latest.barOrigin = r.bar_origin;
    latest.dropped = r.dropped;
    latest.playing = r.playing != 0;
    if (r.serial == latest.serial)
        return latest;

    latest.serial = r.serial;
    latest.kind = r.kind;
    latest.held = r.held != 0;
    latest.pedal = r.pedal != 0;
    latest.root = r.root;
    latest.bass = r.bass;
    latest.pitchClasses = r.pitch_classes;
    latest.notes = noteSet (r.notes);
    latest.sounding = noteSet (r.sounding);
    latest.name = juce::String::fromUTF8 (r.name);
    latest.description = juce::String::fromUTF8 (r.description);
    latest.degree = juce::String::fromUTF8 (r.degree);
    latest.notesText = juce::String::fromUTF8 (r.notes_text);
    latest.alternatives.clearQuick();
    for (int i = 0; i < r.alternative_count && i < CD_ALTERNATIVES; ++i)
        latest.alternatives.add (juce::String::fromUTF8 (r.alternatives[i]));
    return latest;
}

int ChordDetector::takeNotes (NoteEvent* out, int capacity)
{
    const int n = (int) cd_shell_drain (shell.get(), drained.data(),
                                        (size_t) juce::jlimit (0, (int) drained.size(), capacity));
    for (int i = 0; i < n; ++i)
        out[i] = { drained[(size_t) i].at, drained[(size_t) i].note, drained[(size_t) i].velocity };
    return n;
}

WrittenNote ChordDetector::write (int midi) const
{
    const auto w = cd_write_note (choice (Param::tonic), choice (Param::mode), choice (Param::spelling), midi);
    return { w.staff_step, w.accidental, juce::String::fromUTF8 (w.name) };
}

KeyInfo ChordDetector::key() const
{
    const auto k = cd_key_info (choice (Param::tonic), choice (Param::mode));
    return { k.scale, k.signature };
}

juce::AudioProcessorEditor* ChordDetector::createEditor()
{
    return new ni::PluginEditor (*this, std::make_unique<Editor> (*this), Editor::width, Editor::height,
                                 params[Param::zoom], zoomScales());
}

} // namespace ni::chord_detector

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::chord_detector::ChordDetector();
}

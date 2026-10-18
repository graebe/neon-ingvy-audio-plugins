// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Chord-Detector's shell. ChordDetector.h says what it does.
 */
#include "ChordDetector.h"

#include "Editor.h"
#include "EngineParameter.h"
#include "PluginEditor.h"

namespace ni::chord_detector
{

namespace
{
/* The engine's table, as the shared adapter reads it (EngineParameter.h). */
struct Table
{
    using Info = NiParamInfo;
    static bool info (int i, Info* out) { return cd_param_info (i, out); }
    static bool isChoice (const Info& i) { return i.kind == NiParamKind_Choice; }
    static int key (int i, char* b, int n) { return cd_param_key (i, b, n); }
    static int name (int i, char* b, int n) { return cd_param_name (i, b, n); }
    static int label (int i, int option, char* b, int n) { return cd_param_label (i, option, b, n); }
    static int text (int i, double v, char* b, int n) { return cd_param_text (i, v, b, n); }
    static bool parse (int i, const char* t, double* out) { return cd_param_parse (i, t, out); }
    static double normalise (int i, double v) { return cd_param_normalise (i, v); }
    static double denormalise (int i, double n) { return cd_param_denormalise (i, n); }
    static double snap (int i, double v) { return cd_param_snap (i, v); }
};
} // namespace

ChordDetector::ChordDetector()
    : ni::Processor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      shell (cd_shell_create (44100.0))
{
    jassert (cd_param_count() == paramCount);
    for (int i = 0; i < paramCount; ++i)
    {
        auto p = ni::engineParameter<Table> (i);
        params[(size_t) i] = dynamic_cast<juce::AudioParameterChoice*> (p.get());
        jassert (params[(size_t) i] != nullptr);
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

void ChordDetector::prepare (double sampleRate, int)
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

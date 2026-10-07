// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Chord-Detector's editor against a model with nothing behind it: what a
 * reading puts on screen, what each control asks of the host, the notes that
 * reach the history, and the window as a picture, playing and held.
 */
#include "Editor.h"
#include "Pointer.h"
#include "checks.h"
#include "fakes.h"
#include "snapshot.h"

#include <doctest.h>

#include <cstring>
#include <deque>

using namespace ni::chord_detector;
using ni::ui::gallery::Pointer;

namespace
{
/* The engine's table (cd_param_*), as a fake declares it. */
struct FakeModel final : public Model
{
    FakeModel()
    {
        params.addChoice ("tonic", "Key", { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" }, 0);
        params.addChoice ("mode", "Mode", { "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian" }, 0);
        params.addChoice ("spelling", "Spelling", { "Auto", "Sharps", "Flats" }, 0);
        params.addChoice ("hold", "Hold", { "Off", "On" }, 0);
        params.addChoice ("history_view", "History", { "Staff", "MIDI" }, 0);
        params.addChoice ("history_span", "Span", { "1 bar", "2 bars", "4 bars", "8 bars" }, 2);
        params.addChoice ("zoom", "Zoom", { "75%", "100%", "125%", "150%" }, 1);
        now.bpm = 120.0;
        now.bar = 4.0;
        now.scale = 0b1010'1011'0101;
        now.root = -1;
        now.bass = -1;
        std::strcpy (now.key_name, "C Ionian");
    }

    int numParameters() const override { return params.size(); }
    juce::RangedAudioParameter& parameter (int i) override { return params[i]; }
    const CdReading& reading() override { return now; }
    int takeNotes (CdNoteEvent* out, int capacity) override
    {
        int n = 0;
        while (n < capacity && ! notes.empty())
        {
            out[n++] = notes.front();
            notes.pop_front();
        }
        return n;
    }
    CdKey key() const override
    {
        /* The major keys' notes and signatures are all a fake needs. */
        const int tonic = (int) std::lround (params[Param::tonic].getValue() * 11.0f);
        CdKey k {};
        for (int step : { 0, 2, 4, 5, 7, 9, 11 })
            k.scale = (uint16_t) (k.scale | (1u << ((tonic + step) % 12)));
        const int fifths = (tonic * 7) % 12;
        k.signature = (int8_t) (fifths > 6 ? fifths - 12 : fifths);
        return k;
    }

    CdWrittenNote write (int midi) const override
    {
        static constexpr int whites[] = { 0, 2, 4, 5, 7, 9, 11 };
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int pc = midi % 12;
        int letter = 6;
        while (whites[letter] > pc)
            --letter;
        CdWrittenNote w {};
        w.letter = (uint8_t) letter;
        w.accidental = (int8_t) (pc - whites[letter]);
        w.octave = (int16_t) (midi / 12 - 1);
        w.staff_step = w.octave * 7 + letter;
        const auto text = juce::String (names[pc]) + juce::String (w.octave);
        text.copyToUTF8 (w.name, sizeof w.name);
        return w;
    }

    /* A chord, as the engine would publish it after `notes` started. */
    void play (std::initializer_list<int> midi, const char* name, const char* degree, const char* words,
               const char* noteText, int root, double at)
    {
        ++now.serial;
        now.kind = 3;
        now.held = 0;
        now.notes[0] = now.notes[1] = 0;
        now.pitch_classes = 0;
        for (int m : midi)
        {
            now.notes[m / 64] |= 1ull << (m % 64);
            now.pitch_classes = (uint16_t) (now.pitch_classes | (1u << (m % 12)));
            notes.push_back ({ at, (uint8_t) m, 100 });
        }
        now.sounding[0] = now.notes[0];
        now.sounding[1] = now.notes[1];
        now.bass = (int8_t) *midi.begin();
        now.root = (int8_t) root;
        std::strcpy (now.name, name);
        std::strcpy (now.degree, degree);
        std::strcpy (now.description, words);
        std::strcpy (now.notes_text, noteText);
        now.now = at;
    }

    /* Every key up, the reading kept by Hold. */
    void release (double at)
    {
        for (int m = 0; m < 128; ++m)
            if ((now.sounding[m / 64] >> (m % 64)) & 1u)
                notes.push_back ({ at, (uint8_t) m, 0 });
        now.sounding[0] = now.sounding[1] = 0;
        now.held = 1;
        ++now.serial;
        now.now = at;
    }

    ni::ui::test::FakeParameters params;
    CdReading now {};
    std::deque<CdNoteEvent> notes;
};

int choice (FakeModel& m, Param p)
{
    auto& param = m.params[p];
    return (int) std::lround (param.getValue() * (float) (param.getNumSteps() - 1));
}

/* Four bars of a progression, the clock a beat into the fifth. */
void progression (FakeModel& m, Editor& e)
{
    struct Bar { std::initializer_list<int> notes; const char* name; const char* degree; const char* words; const char* text; int root; };
    const Bar bars[] = {
        { { 45, 55, 60, 64 }, "Am7", "vi7", "A minor 7", "A2 G3 C4 E4", 9 },
        { { 50, 53, 57, 60 }, "Dm7", "ii7", "D minor 7", "D3 F3 A3 C4", 2 },
        { { 47, 53, 55, 62 }, "G7/B", "V7", "G dominant 7 · 1st inversion", "B2 F3 G3 D4", 7 },
        { { 48, 55, 59, 64 }, "Cmaj7", "Imaj7", "C major 7", "C3 G3 B3 E4", 0 },
    };
    for (int i = 0; i < 4; ++i)
    {
        if (i > 0)
        {
            m.release (4.0 * i - 0.5);
            m.now.held = 0;
            e.tick();
        }
        m.play (bars[i].notes, bars[i].name, bars[i].degree, juce::CharPointer_UTF8 (bars[i].words).getAddress(),
                bars[i].text, bars[i].root, 4.0 * i);
        e.tick();
    }
    m.now.now = 15.0;
    m.now.playing = 1;
    e.tick();
}
} // namespace

TEST_CASE ("chord-detector: the window is its design's size, and every line fits the bar")
{
    FakeModel model;
    Editor editor (model);
    CHECK (editor.getWidth() == Editor::width);
    CHECK (editor.getHeight() == Editor::height);
    NI_CHECK_INFO_LIMIT (editor);
    CHECK (editor.grandStaff().isVisible());
    CHECK_FALSE (editor.pianoRoll().isVisible());
    CHECK (editor.chordReadout().getState().name.isEmpty());
}

TEST_CASE ("chord-detector: a reading lights the readout, the circle and the keyboard")
{
    FakeModel model;
    Editor editor (model);
    model.play ({ 52, 57, 60, 67 }, "Am7/E", "vi7", "A minor 7 · 2nd inversion", "E3 A3 C4 G4", 9, 1.0);
    editor.tick();

    const auto& words = editor.chordReadout().getState();
    CHECK (words.name == "Am7/E");
    CHECK (words.degree == "vi7");
    CHECK (words.notes == "E3 A3 C4 G4");
    CHECK_FALSE (words.held);

    const auto& ring = editor.circleOfFifths().getState();
    CHECK (ring.root == 9);
    CHECK (ring.lit == ((1u << 4) | (1u << 9) | (1u << 0) | (1u << 7)));
    CHECK (ring.names[10] == "A#");
    CHECK (ring.caption == "no sharps or flats");

    const auto& keys = editor.keyboardView().getState();
    CHECK (keys.lit.test (52));
    CHECK (keys.lit.test (67));
    CHECK (keys.bass == 52);
    CHECK_FALSE (keys.dimmed);

    /* The notes reached the history, and the name is marked where it began. */
    CHECK (editor.noteHistory().notes().size() == 4);
    REQUIRE (editor.noteHistory().marks().size() == 1);
    CHECK (editor.noteHistory().marks().front().text == "Am7/E");
}

TEST_CASE ("chord-detector: a held reading dims the keys it keeps")
{
    FakeModel model;
    Editor editor (model);
    model.play ({ 48, 52, 55 }, "C", "I", "C major", "C3 E3 G3", 0, 0.0);
    editor.tick();
    model.release (2.0);
    editor.tick();

    CHECK (editor.chordReadout().getState().held);
    const auto& keys = editor.keyboardView().getState();
    CHECK (keys.dimmed);
    CHECK (keys.lit.test (52));
    CHECK (editor.circleOfFifths().getState().dimmed);
    for (const auto& n : editor.noteHistory().notes())
        CHECK_FALSE (n.sounding());
}

TEST_CASE ("chord-detector: the controls ask the host, and show what it holds")
{
    FakeModel model;
    Editor editor (model);
    Pointer p;

    /* A key on the circle. */
    auto& circle = editor.circleOfFifths();
    p.click (circle, circle.disc (9).getCentre());
    CHECK (choice (model, Param::tonic) == 9);
    CHECK (circle.getState().tonic == 9);

    /* Spelling: Flats, and the button lit from the parameter. */
    p.click (editor.spellingButton (2), { 10.0f, 10.0f });
    CHECK (choice (model, Param::spelling) == 2);
    CHECK (editor.spellingButton (2).isOn());
    CHECK_FALSE (editor.spellingButton (0).isOn());

    /* Hold. */
    p.click (editor.holdSwitch(), { 7.0f, 14.0f });
    CHECK (choice (model, Param::hold) == 1);
    CHECK (editor.holdSwitch().isOn());

    /* The history as MIDI lines. */
    p.click (editor.viewButton (1), { 10.0f, 10.0f });
    CHECK (choice (model, Param::historyView) == 1);
    CHECK (editor.pianoRoll().isVisible());
    CHECK_FALSE (editor.grandStaff().isVisible());

    /* A host change shows without the editor asking. */
    model.params[Param::mode].setValueNotifyingHost (5.0f / 6.0f);
    CHECK (editor.modeSelect().getIndex() == 5);
    model.params[Param::historySpan].setValueNotifyingHost (0.0f);
    CHECK (editor.spanSelect().getIndex() == 0);
    editor.tick();
    CHECK (editor.grandStaff().getSpan() == doctest::Approx (4.0));
}

TEST_CASE ("chord-detector: a window opened late starts from what sounds, and dropped events are put right")
{
    FakeModel model;
    /* Played before the window opened: a stale stream, and E and G sounding. */
    model.play ({ 52, 55 }, "E–G", "", "minor 3rd", "E3 G3", -1, 1.0);
    model.notes.push_back ({ 0.5, 40, 0 });
    Editor editor (model);
    const auto& seeded = editor.noteHistory().notes();
    REQUIRE (seeded.size() == 2);
    CHECK (seeded[0].sounding());
    CHECK (seeded[1].sounding());

    /* The engine dropped events: E stopped and C started, unheard. */
    model.now.sounding[0] = (1ull << 48) | (1ull << 55);
    model.now.dropped = 7;
    model.now.now = 2.0;
    editor.tick();
    int sounding = 0;
    for (const auto& n : editor.noteHistory().notes())
    {
        if (n.sounding())
        {
            ++sounding;
            CHECK ((n.midi == 48 || n.midi == 55));
        }
    }
    CHECK (sounding == 2);
}

TEST_CASE ("chord-detector: the key is drawn from the parameters at once")
{
    FakeModel model;
    Editor editor (model);
    model.params[Param::tonic].setValueNotifyingHost (5.0f / 11.0f);   // F major, no block run yet
    CHECK (editor.circleOfFifths().getState().tonic == 5);
    CHECK (editor.circleOfFifths().getState().caption == "1 flat");
    CHECK (editor.grandStaff().getSignature() == -1);
}

NI_SNAPSHOT_TEST ("chord-detector: the window, playing, as notation")
{
    FakeModel model;
    Editor editor (model);
    progression (model, editor);
    NI_CHECK_INFO_LIMIT (editor);
    NI_CHECK_SNAPSHOT (editor, "chord-detector-playing");
}

NI_SNAPSHOT_TEST ("chord-detector: the window, held, as MIDI lines")
{
    FakeModel model;
    Editor editor (model);
    model.params[Param::historyView].setValueNotifyingHost (1.0f);
    model.params[Param::hold].setValueNotifyingHost (1.0f);
    progression (model, editor);
    model.release (15.5);
    model.now.now = 15.75;
    model.now.pedal = 1;
    editor.tick();
    NI_CHECK_SNAPSHOT (editor, "chord-detector-held-midi");
}

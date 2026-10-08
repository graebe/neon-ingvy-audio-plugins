// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The music views, each in the states an editor shows it in: the Keyboard,
 * the Circle of Fifths, the Chord Readout, and one history drawn both ways --
 * as a Grand Staff and as a Piano Roll.
 *
 * The music is the same everywhere: a bar each of A minor 7, D minor 7, G 7
 * over B and C major 7 in C major, the last one still sounding.
 */
#include "Gallery.h"

#include "ChordReadout.h"
#include "CircleOfFifths.h"
#include "ControlsPage.h"
#include "GrandStaff.h"
#include "Info.h"
#include "Keyboard.h"
#include "NoteHistory.h"
#include "PianoRoll.h"
#include "UvTokens.h"

namespace
{
using namespace ni::ui;
namespace c = uv::tok::colour;

constexpr InfoText keyInfo { "Key — click a key on the circle, or step it a fifth with the arrows." };

/* The progression, as MIDI, a bar each. */
const std::vector<std::vector<int>> progression {
    { 45, 55, 60, 64 },   // Am7
    { 50, 53, 57, 60 },   // Dm7
    { 47, 53, 55, 62 },   // G7/B
    { 48, 55, 59, 64 },   // Cmaj7
};
const juce::StringArray names { "Am7", "Dm7", "G7/B", "Cmaj7" };

music::NoteSet setOf (const std::vector<int>& notes)
{
    music::NoteSet s;
    for (int n : notes)
        s.set ((size_t) n);
    return s;
}

std::uint16_t classesOf (const std::vector<int>& notes)
{
    std::uint16_t s = 0;
    for (int n : notes)
        s = (std::uint16_t) (s | (1u << (n % 12)));
    return s;
}

/* C major's notes, and D Dorian's: the same seven. */
constexpr std::uint16_t cMajor = 0b1010'1011'0101;

/* Four bars of the progression, the last chord still held, the clock at the
 * start of bar five less a beat. */
void play (NoteHistory& h)
{
    for (size_t bar = 0; bar < progression.size(); ++bar)
    {
        const double at = 4.0 * (double) bar;
        h.mark (at, names[(int) bar]);
        for (int n : progression[bar])
        {
            h.start (n, 100, at);
            if (bar + 1 < progression.size())
                h.stop (n, at + 3.5);
        }
    }
    h.setClock ({ 15.0, 100.0, 4.0, 0.0, true });
}

/* A page that fills a well under each of its views before the headings. */
struct WellPage : public gallery::ControlsPage
{
    void paint (juce::Graphics& g) override
    {
        for (const auto& w : wells)
        {
            g.setColour (c::bg200);
            g.fillRect (w);
            g.setColour (c::line100);
            g.drawRect (w, uv::tok::stroke::strokeHair);
        }
        ControlsPage::paint (g);
    }
    std::vector<juce::Rectangle<float>> wells;
};

/* ------------------------------------------------------------- Keyboard */

struct KeyboardPage final : public WellPage
{
    KeyboardPage()
    {
        const char* headings[] = { "Nothing held", "A minor 7, the bass marked", "Held after release: dimmed" };
        for (int i = 0; i < 3; ++i)
        {
            const int y = i * 104;
            heading (headings[i], 0, y);
            wells.push_back (juce::Rectangle<float> (0.0f, (float) y + 24.0f, 680.0f, 72.0f));
            auto& k = *keys.add (new Keyboard());
            k.setBounds (4, y + 28, 672, 62);
            addAndMakeVisible (k);
        }
        Keyboard::State lit;
        lit.lit = setOf (progression[0]);
        lit.bass = 45;
        keys[1]->setState (lit);
        lit.dimmed = true;
        keys[2]->setState (lit);
        setSize (680, 312);
    }
    juce::OwnedArray<Keyboard> keys;
};

/* ------------------------------------------------------- Circle of Fifths */

struct CirclePage final : public gallery::ControlsPage
{
    CirclePage()
    {
        const char* headings[] = { "C Ionian, nothing sounding", "A minor 7 in C, keyboard focus", "D Dorian, held" };
        for (int i = 0; i < 3; ++i)
        {
            const int x = (i % 2) * 280, y = (i / 2) * 280;
            heading (headings[i], x, y);
            auto& circle = *circles.add (new CircleOfFifths());
            setInfo (circle, keyInfo);
            circle.setBounds (x, y + 24, 232, 232);
            addAndMakeVisible (circle);
        }
        CircleOfFifths::State s;
        s.scale = cMajor;
        s.caption = "no sharps or flats";
        circles[0]->setState (s);

        s.lit = classesOf (progression[0]);
        s.root = 9;
        circles[1]->setState (s);
        showFocus (*circles[1]);

        s.tonic = 2;
        s.dimmed = true;
        s.caption = "D Dorian";
        circles[2]->setState (s);

        note ("the tonic's amber ring is the window's one amber mark", 280, 400);
        setSize (680, 536);
    }
    juce::OwnedArray<CircleOfFifths> circles;
};

/* --------------------------------------------------------- Chord Readout */

struct ReadoutPage final : public gallery::ControlsPage
{
    ReadoutPage()
    {
        for (int i = 0; i < 3; ++i)
        {
            auto& r = *readouts.add (new ChordReadout());
            r.setBounds (0, i * 168 + 24, 432, ChordReadout::idealHeight);
            addAndMakeVisible (r);
        }
        heading ("Nothing sounding", 0, 0);
        heading ("A chord, its numeral and another reading", 0, 168);
        heading ("Held after release", 0, 336);

        ChordReadout::State chord;
        chord.name = "Am7/E";
        chord.degree = "vi7";
        chord.description = juce::String (juce::CharPointer_UTF8 ("A minor 7 \xc2\xb7 2nd inversion"));
        chord.notes = "E3 A3 C4 G4";
        chord.alternatives = { "C6/E" };
        readouts[1]->setState (chord);

        ChordReadout::State held;
        held.name = "G7/B";
        held.degree = "V7";
        held.description = juce::String (juce::CharPointer_UTF8 ("G dominant 7 \xc2\xb7 1st inversion"));
        held.notes = "B2 F3 G3 D4";
        held.held = true;
        readouts[2]->setState (held);
        setSize (680, 496);
    }
    juce::OwnedArray<ChordReadout> readouts;
};

/* ------------------------------------------------------ the two histories */

struct StaffPage final : public WellPage
{
    StaffPage()
    {
        play (history);
        heading ("C major, four bars: the last chord still sounding", 0, 0);
        wells.push_back ({ 0.0f, 24.0f, 680.0f, 136.0f });
        staff.setBounds (0, 24, 680, 136);
        staff.setSpan (16.0);
        addAndMakeVisible (staff);

        /* The same music in E flat major, written with flats: the key
         * signature takes the accidentals the C major notes do not need. */
        heading ("The same notes under three flats", 0, 176);
        wells.push_back ({ 0.0f, 200.0f, 680.0f, 136.0f });
        flats.setBounds (0, 200, 680, 136);
        flats.setSpan (16.0);
        flats.setSignature (-3);
        flats.setWriter ([] (int midi) {
            static constexpr int letters[] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
            static constexpr int alter[] = { 0, -1, 0, -1, 0, 0, -1, 0, -1, 0, -1, 0 };
            const int pc = midi % 12;
            return GrandStaff::Written { (midi / 12 - 1) * 7 + letters[pc], alter[pc] };
        });
        addAndMakeVisible (flats);
        setSize (680, 336);
    }
    NoteHistory history;
    GrandStaff staff { history };
    GrandStaff flats { history };
};

struct RollPage final : public WellPage
{
    RollPage()
    {
        play (history);
        heading ("The same four bars, one numbered line per note", 0, 0);
        wells.push_back ({ 0.0f, 24.0f, 680.0f, 136.0f });
        roll.setBounds (0, 24, 680, 136);
        roll.setSpan (16.0);
        addAndMakeVisible (roll);
        setSize (680, 160);
    }
    NoteHistory history;
    PianoRoll roll { history };
};

} // namespace

NI_GALLERY_PAGE ("Music", "Keyboard", [] { return std::make_unique<KeyboardPage>(); });
NI_GALLERY_PAGE ("Music", "Circle of Fifths", [] { return std::make_unique<CirclePage>(); });
NI_GALLERY_PAGE ("Music", "Chord Readout", [] { return std::make_unique<ReadoutPage>(); });
NI_GALLERY_PAGE ("Music", "Grand Staff", [] { return std::make_unique<StaffPage>(); });
NI_GALLERY_PAGE ("Music", "Piano Roll", [] { return std::make_unique<RollPage>(); });

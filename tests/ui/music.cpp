// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The music views: the Keyboard's geometry and the octaves it follows, the
 * Circle of Fifths' ring and how it asks for a key, the Chord Readout's
 * accessible text, the NoteHistory's bookkeeping, and where the Grand Staff
 * and the Piano Roll put a note. Then every Music page, as a picture.
 */
#include "ChordReadout.h"
#include "CircleOfFifths.h"
#include "Gallery.h"
#include "GrandStaff.h"
#include "Keyboard.h"
#include "NoteHistory.h"
#include "PianoRoll.h"
#include "Pointer.h"
#include "checks.h"
#include "snapshot.h"

#include <doctest.h>

using namespace ni::ui;
using ni::ui::gallery::Pointer;

namespace
{
music::NoteSet notes (std::initializer_list<int> list)
{
    music::NoteSet s;
    for (int n : list)
        s.set ((size_t) n);
    return s;
}
} // namespace

/* ------------------------------------------------------------- Keyboard */

TEST_CASE ("keyboard: white keys share the width, black keys sit over the gap after theirs")
{
    Keyboard k;
    k.setBounds (0, 0, 672, 62);   // four octaves of 24px keys, 56px tall
    const auto c2 = k.keyBounds (36);
    CHECK (c2.getX() == doctest::Approx (0.0));
    CHECK (c2.getWidth() == doctest::Approx (23.0));
    CHECK (c2.getHeight() == doctest::Approx (56.0));

    const auto cSharp = k.keyBounds (37);
    CHECK (cSharp.getCentreX() == doctest::Approx (24.0));
    CHECK (cSharp.getHeight() < c2.getHeight());

    CHECK (k.keyBounds (48).getX() == doctest::Approx (7 * 24.0));   // C3, an octave on
    CHECK (k.keyBounds (35).isEmpty());                              // below its C
    CHECK (k.keyBounds (84).isEmpty());                              // past four octaves
}

TEST_CASE ("keyboard: it stays put while the notes fit, and moves to the C below them when not")
{
    CHECK (Keyboard::lowestToShow ({}, 4, 36) == 36);
    CHECK (Keyboard::lowestToShow (notes ({ 45, 64 }), 4, 36) == 36);
    CHECK (Keyboard::lowestToShow (notes ({ 30, 50 }), 4, 36) == 24);
    CHECK (Keyboard::lowestToShow (notes ({ 90 }), 4, 36) == 84);
    CHECK (Keyboard::lowestToShow (notes ({ 127 }), 4, 36) == 84);   // its octaves reach G9
    CHECK (Keyboard::lowestToShow (notes ({ 127 }), 1, 36) == 120);
    CHECK (Keyboard::lowestToShow (notes ({ 0 }), 4, 36) == 0);
}

TEST_CASE ("keyboard: it reports and takes nothing")
{
    Keyboard k;
    CHECK_FALSE (k.getWantsKeyboardFocus());
    bool selfClicks = true, childClicks = true;
    k.getInterceptsMouseClicks (selfClicks, childClicks);
    CHECK_FALSE (selfClicks);
    Keyboard::State s;
    s.lit = notes ({ 60 });
    k.setState (s);
    CHECK (k.getState() == s);
}

/* ------------------------------------------------------- Circle of Fifths */

TEST_CASE ("circle: C at the top, a fifth further at each step clockwise")
{
    CHECK (CircleOfFifths::pitchAt (0) == 0);
    CHECK (CircleOfFifths::pitchAt (1) == 7);
    CHECK (CircleOfFifths::pitchAt (11) == 5);
    CHECK (CircleOfFifths::pitchAt (-1) == 5);
    for (int pc = 0; pc < 12; ++pc)
        CHECK (CircleOfFifths::pitchAt (CircleOfFifths::placeOf (pc)) == pc);

    CircleOfFifths circle;
    circle.setBounds (0, 0, 232, 232);
    const auto c = circle.disc (0);
    const auto g = circle.disc (7);
    const auto fSharp = circle.disc (6);
    CHECK (c.getCentreX() == doctest::Approx (116.0));
    CHECK (c.getY() >= 0.0f);
    CHECK (g.getCentreX() > c.getCentreX());
    CHECK (fSharp.getCentreY() > 116.0f);   // at the bottom
    CHECK (circle.centre().getCentre().x == doctest::Approx (116.0));
    for (int pc = 0; pc < 12; ++pc)
        CHECK_FALSE (circle.centre().intersects (circle.disc (pc)));
}

TEST_CASE ("circle: a click on a key asks for it; the state is the owner's to change")
{
    CircleOfFifths circle;
    circle.setBounds (0, 0, 232, 232);
    int asked = -1;
    circle.onTonicSelected = [&] (int pc) { asked = pc; };

    Pointer p;
    p.click (circle, circle.disc (9).getCentre());
    CHECK (asked == 9);
    CHECK (circle.getState().tonic == 0);

    asked = -1;
    p.click (circle, circle.centre().getCentre());
    CHECK (asked == -1);

    circle.mouseDown (Pointer::event (circle, circle.disc (2).getCentre(), juce::ModifierKeys::rightButtonModifier));
    CHECK (asked == -1);
}

TEST_CASE ("circle: the arrows step a fifth, Home goes to C")
{
    CircleOfFifths circle;
    std::vector<int> asked;
    circle.onTonicSelected = [&] (int pc) {
        asked.push_back (pc);
        auto s = circle.getState();
        s.tonic = pc;
        circle.setState (s);
    };
    CHECK (circle.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (circle.keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
    CHECK (circle.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (circle.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (circle.keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (circle.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    CHECK_FALSE (circle.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (asked == std::vector<int> { 7, 2, 7, 0, 5, 0 });
}

TEST_CASE ("circle: to an accessibility client the tonic is a value, read and set by name")
{
    CircleOfFifths circle;
    auto handler = circle.createAccessibilityHandler();
    auto* value = handler->getValueInterface();
    REQUIRE (value != nullptr);
    CHECK (value->getCurrentValueAsString() == "C");

    int asked = -1;
    circle.onTonicSelected = [&] (int pc) { asked = pc; };
    value->setValueAsString ("Bb");
    CHECK (asked == 10);
    value->setValue (1.0);
    CHECK (asked == 7);
    value->setValueAsString ("H");
    CHECK (asked == 7);
}

/* --------------------------------------------------------- Chord Readout */

TEST_CASE ("chord readout: its accessible description is what it shows")
{
    ChordReadout r;
    CHECK (r.getDescription().isEmpty());
    ChordReadout::State s;
    s.name = "Am7";
    s.description = "A minor 7";
    r.setState (s);
    CHECK (r.getDescription() == "Am7, A minor 7");
    r.setState ({});
    CHECK (r.getDescription() == "Nothing is sounding");
}

/* ----------------------------------------------------------- NoteHistory */

TEST_CASE ("history: a note sounds from its start to its stop, once")
{
    NoteHistory h;
    h.start (60, 90, 1.0);
    h.start (60, 90, 1.5);   // already sounding
    h.stop (61, 2.0);        // never started
    REQUIRE (h.notes().size() == 1);
    CHECK (h.notes().front().sounding());
    h.stop (60, 2.0);
    CHECK_FALSE (h.notes().front().sounding());
    CHECK (h.notes().front().end == doctest::Approx (2.0));

    h.start (60, 80, 3.0);   // again, after it stopped
    h.start (64, 80, 3.0);
    h.stopAll (3.5);
    CHECK (h.notes().size() == 3);
    for (const auto& n : h.notes())
        CHECK_FALSE (n.sounding());

    h.start (200, 1, 0.0);
    CHECK (h.notes().size() == 3);
}

TEST_CASE ("history: what ended long enough ago goes as the clock moves")
{
    NoteHistory h (8.0);
    h.start (48, 100, 0.0);   // held throughout
    h.start (60, 100, 0.0);
    h.stop (60, 1.0);
    h.start (62, 100, 9.0);
    h.stop (62, 9.5);
    h.mark (0.0, "C");
    h.mark (0.5, "C");       // the same name: the same chord
    h.mark (9.0, "Dm");
    h.mark (12.0, "G");
    CHECK (h.marks().size() == 3);

    h.setClock ({ 12.0, 120.0, 4.0, 0.0, true });
    REQUIRE (h.notes().size() == 2);
    CHECK (h.notes()[0].midi == 48);
    CHECK (h.notes()[1].midi == 62);
    /* The last name before the window stays: it names the left edge. */
    CHECK (h.marks().size() == 3);
    h.setClock ({ 30.0, 120.0, 4.0, 0.0, true });
    CHECK (h.marks().size() == 1);

    CHECK (h.range (0.0) == std::pair<int, int> { 48, 48 });
    h.clear();
    CHECK (h.range (0.0) == std::pair<int, int> { -1, -1 });
}

/* --------------------------------------------------- the two history views */

TEST_CASE ("staff: middle C between the staves, E4 on the treble's bottom line, sharps by default")
{
    NoteHistory h;
    GrandStaff staff (h);
    staff.setBounds (0, 0, 696, 136);
    const float trebleBottom = staff.yOfStep (30);
    const float middleC = staff.yOfStep (28);
    const float bassTop = staff.yOfStep (26);
    CHECK (middleC - trebleBottom == doctest::Approx (GrandStaff::space));
    CHECK (bassTop - middleC > GrandStaff::space);

    CHECK (staff.write (60).step == 28);
    CHECK (staff.write (61).step == 28);
    CHECK (staff.write (61).alteration == 1);
    CHECK (staff.write (59).step == 27);

    staff.setWriter ([] (int midi) { return GrandStaff::Written { 99, midi == 61 ? -1 : 0 }; });
    CHECK (staff.write (61).alteration == -1);
    staff.setSignature (12);
    CHECK (staff.getSignature() == 7);
}

TEST_CASE ("roll: at least 24 lines around what was played, inside MIDI's range")
{
    NoteHistory h;
    PianoRoll roll (h);
    roll.setBounds (0, 0, 696, 136);
    roll.setSpan (16.0);
    CHECK (roll.lines() == std::pair<int, int> { 48, 72 });   // nothing played: around middle C

    h.start (40, 100, 0.0);
    h.start (90, 100, 0.0);
    h.setClock ({ 1.0, 120.0, 4.0, 0.0, true });
    CHECK (roll.lines() == std::pair<int, int> { 38, 92 });
    CHECK (roll.lineOf (90).getY() < roll.lineOf (40).getY());

    h.clear();
    h.start (1, 100, 0.0);
    CHECK (roll.lines().first == 0);
    CHECK (roll.lines().second == 24);
    h.clear();
    h.start (127, 100, 0.0);
    CHECK (roll.lines() == std::pair<int, int> { 103, 127 });
}

/* ----------------------------------------------------------------- pictures */

NI_SNAPSHOT_TEST ("music: every page, as a picture")
{
    for (const char* name : { "music-keyboard", "music-circle-of-fifths", "music-chord-readout", "music-grand-staff",
                              "music-piano-roll" })
    {
        CAPTURE (name);
        ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
        REQUIRE (frame.show (juce::String (name)));
        REQUIRE (frame.page() != nullptr);
        NI_CHECK_INFO_LIMIT (*frame.page());
        NI_CHECK_SNAPSHOT (*frame.page(), name);
    }
}

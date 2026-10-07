// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Spectrogram's editor, behaviour by behaviour, as its user manual
 * (plugins/spectrogram/README.md, docs/live.md) and the web editor have it:
 * it opens on the session and pushes nothing; columns arrive and stop ("no
 * signal"); the zoom clears, unpauses and asks the plugin; pause holds the
 * picture while the columns keep coming; the bar view places columns by the
 * host's position and draws bars under the picture; the view ADDS channels
 * and clears the history, the comparison is a separate thing, the clash asks
 * for its criteria and opens the buses it measures; a bus at another rate is
 * shown and refused; a session the host loads, a bus inserted and a new axis
 * all show; the crosshair reads the picture; and the window is laid out,
 * scaled and labelled as 1.1.0 has it.
 *
 * Every test drives the editor through its controls -- pointer and keys, as
 * a person would -- and reads the result back from the fake model.
 */
#include "SpectrogramEditor.h"

#include "Channels.h"
#include "InfoLines.h"
#include "UvTokens.h"
#include "UvType.h"
#include "Pointer.h"
#include "WaveSource.h"
#include "checks.h"
#include "spectrogram_fake.h"

#include <doctest.h>

using namespace ni::spectrogram;
using ni::ui::gallery::Pointer;
using ni::ui::gallery::key;
using ni::ui::gallery::space;
using test::FakeModel;

namespace
{
/* An editor on a clock the test moves, its Ground at rest. */
struct Rig
{
    double ms = 1000.0;
    FakeModel model;
    std::unique_ptr<SpectrogramEditor> editor;

    explicit Rig (std::function<void (FakeModel&)> prepare = {})
    {
        if (prepare)
            prepare (model);
        editor = std::make_unique<SpectrogramEditor> (model, [this] { return ms; });
        editor->frame().ground().setReducedMotionQuery ([] { return false; });
    }

    SpectrogramView& view() { return editor->view(); }
    ni::ui::Spectrogram& picture() { return view().picture(); }

    /* One display frame, `dt` ms after the last. */
    void frame (double dt = 1000.0 / 60.0)
    {
        ms += dt;
        editor->frame().frameClock().tick();
    }
};

std::vector<Source> threeBuses()
{
    return { { 1, true, 48000, "bass" }, { 2, true, 48000, "pad" }, { 4, true, 96000, "far" } };
}
} // namespace

/* ---------------------------------------------------------------- session */

TEST_CASE ("spectrogram editor: it opens on the session the plugin holds, and pushes nothing")
{
    Rig rig ([] (FakeModel& m) {
        m.buses = threeBuses();
        m.look.rangeLo = 40.0f;
        m.look.rangeHi = 800.0f;
        m.look.view = { 0, 2 };
        m.look.compareA = 1;
        m.look.compareB = 2;
        m.look.clash = true;
    });
    auto& v = rig.view();

    CHECK (v.rangeSelect().getIndex() == 2);
    CHECK (v.rangeSelect().getOptions()[2] == "Bass");
    CHECK (v.viewList().getSelected() == std::vector<int> { 0, 2 });
    CHECK (v.viewList().getChildComponent (0)->getTitle() == "input, pad");
    CHECK (v.compareSelect().getOptions().joinIntoString ("|") == "input|bass|pad|far");
    CHECK (v.compareSelect().getIndex() == 1);
    CHECK (v.againstSelect().getIndex() == 2);
    CHECK (v.clashSwitch().isOn());
    CHECK (rig.picture().isClashShown());

    /* Not saved, so a reopened window starts live, in seconds. */
    CHECK_FALSE (v.isPaused());
    CHECK_FALSE (v.isBarView());
    CHECK (v.bars() == 4);

    rig.frame();
    rig.frame();
    CHECK (rig.model.ranges.empty());
    CHECK (rig.model.looks.empty());
    CHECK (rig.model.criteria.empty());
}

TEST_CASE ("spectrogram editor: a session the host loads while the window is open shows, and clears a new mix")
{
    Rig rig ([] (FakeModel& m) { m.buses = threeBuses(); });
    rig.model.queue (1, 200);
    rig.frame();
    CHECK (rig.picture().levelAt (ni::ui::Spectrogram::View::scroll, 0, 10) == 200);

    /* The comparison alone: the picture stays. */
    rig.model.look.compareA = 2;
    rig.model.look.clash = true;
    rig.frame();
    CHECK (rig.view().compareSelect().getIndex() == 2);
    CHECK (rig.view().clashSwitch().isOn());
    CHECK (rig.picture().levelAt (ni::ui::Spectrogram::View::scroll, 0, 10) == 200);

    /* A new view is another mix: thirteen seconds of the old one go. */
    rig.model.look.view = { 0, 1 };
    rig.model.look.rangeLo = 2000.0f;
    rig.model.look.rangeHi = 20000.0f;
    rig.frame();
    CHECK (rig.view().viewList().getSelected() == std::vector<int> { 0, 1 });
    CHECK (rig.view().rangeSelect().getIndex() == 4);
    CHECK (rig.picture().levelAt (ni::ui::Spectrogram::View::scroll, 0, 10) == 0);
    CHECK (rig.model.looks.empty());
}

/* ---------------------------------------------------------------- columns */

TEST_CASE ("spectrogram editor: columns go into the picture; half a second without any is no signal")
{
    Rig rig;
    auto& status = rig.view().status();
    /* Nothing has arrived yet: the window says so from the start. */
    CHECK (status.getText() == "no signal");
    CHECK_FALSE (rig.view().isLive());

    rig.model.queue (3, 128);
    rig.frame();
    CHECK (rig.view().isLive());
    CHECK (status.getText().isEmpty());
    CHECK (rig.picture().getCursor() == 3);
    CHECK (rig.picture().levelAt (ni::ui::Spectrogram::View::scroll, 2, 0) == 128);

    /* Frames with nothing new, still within the half second: live. */
    rig.frame (200.0);
    rig.frame (200.0);
    CHECK (rig.view().isLive());
    /* Past it: the amber word. */
    rig.frame (200.0);
    CHECK_FALSE (rig.view().isLive());
    CHECK (status.getText() == "no signal");

    /* And back as soon as a column comes. */
    rig.model.queue (1, 90);
    rig.frame();
    CHECK (rig.view().isLive());
}

TEST_CASE ("spectrogram editor: the clash mask is drawn only over the columns it was measured for")
{
    Rig rig ([] (FakeModel& m) { m.look.clash = true; });
    rig.model.queue (2, 120, true, 220);
    rig.frame();
    CHECK (rig.picture().isClashShown());
    CHECK (rig.model.pending.empty());
    CHECK (rig.picture().getCursor() == 2);
}

TEST_CASE ("spectrogram editor: a new axis moves the scale's marks; the zoom's own axis comes from the plugin")
{
    Rig rig;
    const auto full = rig.view().frequencyScale().getMarks();
    REQUIRE (full.size() == 9);
    CHECK (full.front().label == "10");
    CHECK (full.back().label == "20k");

    /* The plugin re-banded for Mid. */
    rig.model.hz.clear();
    for (int i = 0; i < 256; ++i)
        rig.model.hz.push_back ((float) (200.0 * std::pow (20.0, (i + 0.5) / 256.0)));
    rig.frame();
    const auto mid = rig.view().frequencyScale().getMarks();
    REQUIRE (mid.size() == 2);
    CHECK (mid.front().label == "500");
}

/* -------------------------------------------------------------- the zoom */

TEST_CASE ("spectrogram editor: the range zooms -- the picture clears, unpauses, and the plugin is asked")
{
    Rig rig;
    auto& v = rig.view();
    rig.model.queue (4, 180);
    rig.frame();
    Pointer().click (v.pauseButton(), { 14.0f, 14.0f });
    REQUIRE (v.isPaused());

    /* By keys: Down opens the list on Full, two rows down is Bass, Enter. */
    CHECK (key (v.rangeSelect(), juce::KeyPress::downKey));
    REQUIRE (v.rangeSelect().isOpen());
    key (v.rangeSelect(), juce::KeyPress::downKey);
    key (v.rangeSelect(), juce::KeyPress::downKey);
    key (v.rangeSelect(), juce::KeyPress::returnKey);

    REQUIRE (rig.model.ranges.size() == 1);
    CHECK (rig.model.ranges[0].lo == 40.0f);
    CHECK (rig.model.ranges[0].hi == 800.0f);
    CHECK (v.rangeSelect().getIndex() == 2);
    CHECK_FALSE (v.isPaused());
    CHECK_FALSE (rig.picture().isPaused());
    CHECK_FALSE (v.pauseButton().isOn());
    CHECK (rig.picture().getCursor() == 0);
    CHECK (rig.picture().levelAt (ni::ui::Spectrogram::View::scroll, 3, 0) == 0);
}

/* ------------------------------------------------------------------ pause */

TEST_CASE ("spectrogram editor: pause holds the picture while the columns keep arriving behind it")
{
    Rig rig;
    auto& v = rig.view();
    auto& pause = v.pauseButton();
    CHECK (pause.getTitle() == "Pause the picture");
    CHECK (pause.getIcon() == "pause");

    space (pause);
    CHECK (v.isPaused());
    CHECK (rig.picture().isPaused());
    CHECK (pause.isOn());
    CHECK (pause.getTitle() == "Resume the picture");
    /* The hint says what a press does now, as the tooltip did. */
    CHECK (ni::ui::infoOf (pause) == info::resume.str());

    /* The analysis is never told: columns are taken and written on. */
    rig.model.queue (5, 77);
    rig.frame();
    CHECK (rig.model.pending.empty());
    CHECK (rig.picture().getCursor() == 5);
    CHECK (v.isLive());

    Pointer().click (pause, { 14.0f, 14.0f });
    CHECK_FALSE (v.isPaused());
    CHECK_FALSE (pause.isOn());
    CHECK (ni::ui::infoOf (pause) == info::pause.str());
    CHECK (rig.model.ranges.empty());
    CHECK (rig.model.looks.empty());
}

/* -------------------------------------------------------------- bar view */

TEST_CASE ("spectrogram editor: a Span in bars draws the host's bars, and places columns by position")
{
    Rig rig;
    auto& v = rig.view();
    rig.model.clock.ppq = 6.0;          // bar 2, beat 3 of a 4-bar window
    rig.model.clock.ppqPerColumn = 0.5;

    CHECK (v.timeAxis().getMarks().front().label == "0 s");
    CHECK (v.readout().getTimeKey() == "time");

    /* One setting, seconds or bars: its face is the history the picture
     * holds. By keys: Down opens it on 13 s, three rows down is 4 bars. */
    auto& span = v.spanSelect();
    CHECK (span.getOptions().joinIntoString ("|") == "13 s|1 bar|2 bars|4 bars|8 bars|16 bars");
    CHECK (span.getIndex() == 0);
    key (span, juce::KeyPress::downKey);
    for (int i = 0; i < 3; ++i)
        key (span, juce::KeyPress::downKey);
    key (span, juce::KeyPress::returnKey);
    CHECK (v.isBarView());
    CHECK (span.getIndex() == 3);
    CHECK (v.bars() == 4);
    CHECK (rig.picture().getView() == ni::ui::Spectrogram::View::bars);
    CHECK (v.timeAxis().isBarView());
    CHECK (v.readout().getTimeKey() == "pos");
    int barTicks = 0;
    for (const auto& m : v.timeAxis().getMarks())
        barTicks += m.beat ? 0 : 1;
    CHECK (barTicks == 4);

    /* Three columns, back-dated from the newest: 5, 5.5 and 6 quarters in. */
    rig.model.queue (3, 150);
    rig.frame();
    CHECK (rig.picture().getPlayhead() == slotForPpq (6.0, 4, 4, 4, pictureWidth));
    CHECK (rig.picture().levelAt (ni::ui::Spectrogram::View::bars, slotForPpq (5.0, 4, 4, 4, pictureWidth), 0) == 150);

    /* How many bars: by keys, opened on 4 bars, Up to 2. */
    key (span, juce::KeyPress::upKey);
    key (span, juce::KeyPress::upKey);
    key (span, juce::KeyPress::returnKey);
    CHECK (v.bars() == 2);
    CHECK (span.getIndex() == 2);
    barTicks = 0;
    for (const auto& m : v.timeAxis().getMarks())
        barTicks += m.beat ? 0 : 1;
    CHECK (barTicks == 2);

    /* A metre change redraws the grid: 6/8 is three quarters a bar. */
    rig.model.clock.numerator = 6;
    rig.model.clock.denominator = 8;
    rig.frame();
    int beats = 0;
    for (const auto& m : v.timeAxis().getMarks())
        beats += m.beat ? 1 : 0;
    CHECK (beats == 4);

    /* Switching back loses nothing and asks the plugin nothing; the bar
     * view's width is kept for the next time. */
    key (span, juce::KeyPress::downKey);
    key (span, juce::KeyPress::homeKey);
    key (span, juce::KeyPress::returnKey);
    CHECK_FALSE (v.isBarView());
    CHECK (span.getIndex() == 0);
    CHECK (v.bars() == 2);
    CHECK (rig.picture().getView() == ni::ui::Spectrogram::View::scroll);
    CHECK (rig.picture().getCursor() == 3);
    CHECK (rig.model.looks.empty());
}

TEST_CASE ("spectrogram editor: the facts the hint gave up sit in the toolbar, and say `free` off the playhead")
{
    Rig rig ([] (FakeModel& m) { m.hz.clear(); });
    auto& v = rig.view();
    auto& facts = v.facts();
    CHECK (facts.getText() == "waiting for the plugin");

    /* The axis arrives: the span it really drew, and the floor. */
    for (int i = 0; i < 256; ++i)
        rig.model.hz.push_back ((float) (10.0 * std::pow (1970.0, (i + 0.5) / 256.0)));
    rig.frame();
    CHECK (facts.getText() == pictureFacts (rig.model.hz, false, 120.0, false));
    CHECK (facts.getText().endsWith (juce::String::fromUTF8 ("floor \xe2\x88\x92" "96 dB")));
    CHECK_FALSE (facts.getText().contains ("BPM"));

    /* The bar view adds the tempo, `free` while no playhead drives it... */
    rig.model.clock.bpm = 127.5;
    rig.frame();
    v.spanSelect().onChange (3);
    CHECK (facts.getText().endsWith ("127.5 BPM free"));

    /* ...and drops it when the host's transport runs. */
    rig.model.clock.running = true;
    rig.frame();
    CHECK (facts.getText().endsWith ("127.5 BPM"));

    v.spanSelect().onChange (0);
    CHECK_FALSE (facts.getText().contains ("BPM"));
}

/* --------------------------------------------------------- view, compare */

TEST_CASE ("spectrogram editor: ticking channels ADDS them to the picture and clears the history")
{
    Rig rig ([] (FakeModel& m) { m.buses = threeBuses(); });
    auto& v = rig.view();
    rig.model.queue (2, 140);
    rig.frame();

    Pointer p;
    p.click (*v.viewList().getChildComponent (0), { 20.0f, 14.0f });
    REQUIRE (v.viewList().isOpen());
    auto* bass = v.viewList().getSwitch (1);
    REQUIRE (bass != nullptr);
    p.click (*bass, { 7.0f, 14.0f });

    REQUIRE (rig.model.looks.size() == 1);
    CHECK (rig.model.looks[0].view == std::vector<int> { 0, 1 });
    /* The bus the view now shows is opened; the comparison is off. */
    CHECK (rig.model.looks[0].listen == std::vector<int> { 1 });
    CHECK_FALSE (rig.model.looks[0].clash);
    CHECK (v.viewList().getSelected() == std::vector<int> { 0, 1 });
    CHECK (v.viewList().getChildComponent (0)->getTitle() == "input, bass");
    CHECK (rig.picture().getCursor() == 0);

    /* Unticking everything is the own channel, never nothing. */
    p.click (*v.viewList().getSwitch (0), { 7.0f, 14.0f });
    p.click (*v.viewList().getSwitch (1), { 7.0f, 14.0f });
    CHECK (rig.model.looks.back().view == std::vector<int> { 0 });
    CHECK (rig.model.looks.back().listen.empty());
}

TEST_CASE ("spectrogram editor: a bus at another rate is listed under View, greyed, and cannot be ticked")
{
    Rig rig ([] (FakeModel& m) { m.buses = threeBuses(); });
    auto& v = rig.view();
    const auto& options = v.viewList().getOptions();
    REQUIRE (options.size() == 4);
    CHECK (options[3].name == "far");
    CHECK (options[3].disabled);
    CHECK (options[3].hint == "96k");

    v.viewList().open();
    auto* far = v.viewList().getSwitch (3);
    REQUIRE (far != nullptr);
    CHECK_FALSE (far->isEnabled());
    Pointer().click (*far, { 7.0f, 14.0f });
    CHECK (rig.model.looks.empty());
}

TEST_CASE ("spectrogram editor: the comparison is its own setting, and the clash opens what it measures")
{
    Rig rig ([] (FakeModel& m) { m.buses = threeBuses(); });
    auto& v = rig.view();
    rig.model.queue (2, 140);
    rig.frame();

    /* Against: bass vs pad. Changing the comparison leaves the picture. */
    key (v.againstSelect(), juce::KeyPress::downKey);
    key (v.againstSelect(), juce::KeyPress::downKey);
    key (v.againstSelect(), juce::KeyPress::returnKey);
    REQUIRE (rig.model.looks.size() == 1);
    CHECK (rig.model.looks[0].b == 2);
    CHECK (rig.model.looks[0].view == std::vector<int> { 0 });
    CHECK (rig.model.looks[0].listen.empty());
    CHECK (rig.picture().getCursor() == 2);

    v.compareSelect().onChange (1);
    CHECK (rig.model.looks.back().a == 1);
    CHECK (v.compareSelect().getIndex() == 1);

    /* Clash on: the criteria first, then both ends of the comparison opened,
     * though neither is in the view. */
    Pointer().click (v.clashSwitch(), { 7.0f, 14.0f });
    REQUIRE (rig.model.criteria.size() == 1);
    CHECK (rig.model.criteria[0].floorDb == -60.0f);
    CHECK (rig.model.criteria[0].balanceDb == 12.0f);
    CHECK (rig.model.looks.back().clash);
    CHECK (rig.model.looks.back().listen == std::vector<int> { 1, 2 });
    CHECK (v.clashSwitch().isOn());
    CHECK (rig.picture().isClashShown());

    space (v.clashSwitch());
    CHECK_FALSE (rig.model.looks.back().clash);
    CHECK (rig.model.looks.back().listen.empty());
    CHECK_FALSE (rig.picture().isClashShown());
}

TEST_CASE ("spectrogram editor: a compared channel with no bus shows as none, and picking input sends")
{
    /* The session's default compares input against channel 1; with no
     * Listen-In there is no channel 1. */
    Rig rig;
    auto& v = rig.view();
    CHECK (v.compareSelect().getIndex() == 0);
    CHECK (v.againstSelect().getIndex() == -1);

    /* Down opens the list with no row lit, Down again lights input. */
    key (v.againstSelect(), juce::KeyPress::downKey);
    key (v.againstSelect(), juce::KeyPress::downKey);
    key (v.againstSelect(), juce::KeyPress::returnKey);
    REQUIRE (rig.model.looks.size() == 1);
    CHECK (rig.model.looks[0].b == 0);
    CHECK (v.againstSelect().getIndex() == 0);
}

TEST_CASE ("spectrogram editor: a Listen-In inserted while the window is open joins both lists by its name")
{
    Rig rig;
    auto& v = rig.view();
    CHECK (v.compareSelect().getOptions().joinIntoString ("|") == "input");
    CHECK (v.viewList().getOptions().size() == 1);

    rig.model.buses = { { 3, true, 48000, "" } };
    rig.frame();
    CHECK (v.compareSelect().getOptions().joinIntoString ("|") == "input|Bus 3");
    CHECK (v.againstSelect().getOptions().joinIntoString ("|") == "input|Bus 3");
    CHECK (v.againstSelect().getIndex() == 1);
    REQUIRE (v.viewList().getOptions().size() == 2);
    CHECK (v.viewList().getOptions()[1].name == "Bus 3");

    /* A muted one is listed nowhere: channels are the live buses. */
    rig.model.buses.push_back ({ 4, false, 48000, "muted" });
    rig.frame();
    CHECK (v.viewList().getOptions().size() == 2);

    /* The session's rate decides what is refused once it is known. */
    rig.model.clock.sampleRate = 44100;
    rig.frame();
    CHECK (v.viewList().getOptions()[1].disabled);
    CHECK (v.viewList().getOptions()[1].hint == "48k");
}

/* -------------------------------------------------------------- crosshair */

TEST_CASE ("spectrogram editor: the crosshair reads the picture -- frequency, time or position, level")
{
    Rig rig;
    auto& v = rig.view();
    auto& r = v.readout();
    CHECK (r.getFrequency() == noReading());
    CHECK (r.getTime() == noReading());
    CHECK (r.getLevel() == noReading());

    rig.model.queue (1, 0);
    rig.model.queue (1, 208);
    rig.frame();
    rig.frame();

    /* The newest column, at the right edge; band 0 at the bottom. */
    Pointer p;
    p.enter (rig.picture(), { 605.5f, 255.5f });
    p.move (rig.picture(), { 605.5f, 255.5f });
    CHECK (r.getFrequency() == asHz (rig.model.hz[0]));
    CHECK (r.getTime() == "now");
    CHECK (r.getLevel() == juce::String::fromUTF8 ("\xe2\x88\x92" "17.7 dB"));

    /* One column older, at the floor. */
    p.move (rig.picture(), { 604.5f, 255.5f });
    CHECK (r.getLevel() == juce::String::fromUTF8 ("< \xe2\x88\x92" "96 dB"));
    CHECK (r.getTime() == juce::String::fromUTF8 ("\xe2\x88\x92" "0.02 s"));

    /* The bar view says where in the bars. */
    v.spanSelect().onChange (3);
    CHECK (r.getTimeKey() == "pos");
    CHECK (r.getTime().containsChar (':'));

    p.exit (rig.picture());
    CHECK (r.getFrequency() == noReading());
    CHECK (r.getLevel() == noReading());
}

TEST_CASE ("spectrogram editor: the keys read the picture too -- Tab onto it, the arrows move the crosshair")
{
    Rig rig;
    auto& v = rig.view();
    auto& r = v.readout();
    rig.model.queue (2, 208);
    rig.frame();

    rig.picture().focusGained (juce::Component::focusChangedByTabKey);
    CHECK (r.getFrequency() != noReading());
    CHECK (r.getTime() != noReading());

    /* End is the newest column. */
    CHECK (key (rig.picture(), juce::KeyPress::endKey));
    CHECK (r.getTime() == "now");

    CHECK (key (rig.picture(), juce::KeyPress::escapeKey));
    CHECK (r.getFrequency() == noReading());
}

/* ----------------------------------------------------------------- window */

TEST_CASE ("spectrogram editor: 720 x 502, the content in the frame's padding, the rows on the 4 px grid")
{
    Rig rig;
    auto& e = *rig.editor;
    auto& v = rig.view();
    CHECK (SpectrogramEditor::designWidth == 720);
    CHECK (SpectrogramEditor::designHeight == 502);
    CHECK (e.getWidth() == 720);
    CHECK (e.getHeight() == 502);
    CHECK (v.getBounds() == juce::Rectangle<int> (32, 32, 656, 402));

    /* The toolbar: Pause on the right edge, Span (100) and Range (96)
     * space-4 apart before it, the facts ending space-4 before Range, past
     * the amber word's room. */
    CHECK (v.pauseButton().getBounds() == juce::Rectangle<int> (628, 0, 28, 28));
    CHECK (v.spanSelect().getBounds() == juce::Rectangle<int> (512, 0, 100, 28));
    CHECK (v.rangeSelect().getBounds() == juce::Rectangle<int> (400, 0, 96, 28));
    CHECK (v.facts().getRight() == 384);
    /* Every option shows whole, "16 bars" included. */
    const auto valueFont = uv::type::font (uv::tok::type::value);
    for (const auto& option : v.spanSelect().getOptions())
        CHECK (uv::type::width (valueFont, option) <= (float) (v.spanSelect().field().getWidth() - 2 - 12 - 28));
    CHECK (v.facts().getX() == v.status().getRight() + 16);

    /* The strip, space-4 under it: View on the left, the clash switch on the
     * right edge, the rule between. */
    CHECK (v.viewList().getY() == 44);
    CHECK (v.viewList().getWidth() == 150);
    CHECK (v.clashSwitch().getRight() == 656);
    CHECK (v.compareSelect().getWidth() == 112);
    CHECK (v.againstSelect().getWidth() == 112);

    /* The display: the scale's 40, space-2, the well; the picture a hairline
     * in; the axis and the readout on the picture's x. */
    CHECK (v.frequencyScale().getBounds() == juce::Rectangle<int> (0, 80, 40, 274));
    CHECK (v.well().getBounds() == juce::Rectangle<int> (48, 88, 608, 258));
    CHECK (rig.picture().getBounds() == juce::Rectangle<int> (1, 1, 606, 256));
    CHECK (v.timeAxis().getX() == 49 - TimeAxis::overhang);
    CHECK (v.timeAxis().getY() == 354);
    CHECK (v.readout().getBounds() == juce::Rectangle<int> (49, 382, 607, 20));

    /* The well is the wall the rings break around, the picture inside it
     * part of it. */
    CHECK (ni::ui::isWaveSource (v.well()));
    CHECK_FALSE (ni::ui::isWaveSource (rig.picture()));
}

TEST_CASE ("spectrogram editor: a fixed design, scaled to whatever the host gives")
{
    Rig rig;
    rig.editor->setSize (1080, 753);
    CHECK (rig.editor->fit().getScale() == doctest::Approx (1.5));
    /* Laid out at its design size, always. */
    CHECK (rig.editor->frame().getWidth() == 720);
    CHECK (rig.view().getWidth() == 656);
}

TEST_CASE ("spectrogram editor: the bar states conventions; every control says what it does in 72")
{
    Rig rig;
    auto& frame = rig.editor->frame();
    const auto content = frame.hint().content();
    REQUIRE (content.clauses.size() == 2);
    CHECK (content.clauses[0] == ni::ui::Clause { "hover", "to read a point" });
    CHECK (content.clauses[1] == ni::ui::Clause { "clash", "marks where channels collide" });

    auto& v = rig.view();
    CHECK (ni::ui::infoOf (v.rangeSelect()) == info::range.str());
    CHECK (ni::ui::infoOf (v.spanSelect()) == info::span.str());
    CHECK (ni::ui::infoOf (v.pauseButton()) == info::pause.str());
    CHECK (ni::ui::infoOf (v.viewList()) == info::view.str());
    CHECK (ni::ui::infoOf (v.compareSelect()) == info::compare.str());
    CHECK (ni::ui::infoOf (v.againstSelect()) == info::against.str());
    CHECK (ni::ui::infoOf (v.clashSwitch()) == info::clash.str());
    CHECK (ni::ui::infoOf (rig.picture()) == info::picture.str());
    CHECK (ni::ui::infoOf (frame.motionSwitch()) == info::motion.str());
    NI_CHECK_INFO_LIMIT (frame);

    /* Every state's lines, the bar view and an open list included. */
    v.spanSelect().onChange (3);
    v.viewList().open();
    NI_CHECK_INFO_LIMIT (frame);

    /* The pointer over a control puts its line in the bar. */
    frame.infoState().enter (info::clash, &v.clashSwitch());
    REQUIRE (frame.hint().content().info.has_value());
    CHECK (frame.hint().content().info->name == "Clash");
}

TEST_CASE ("spectrogram editor: Tab goes through the controls in reading order, and stays in the window")
{
    Rig rig;
    auto& v = rig.view();
    auto& frame = rig.editor->frame();
    CHECK (frame.isKeyboardFocusContainer());

    juce::KeyboardFocusTraverser traverser;
    const auto order = traverser.getAllComponents (&frame);
    const auto indexOf = [&] (juce::Component& c) {
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] == &c || c.isParentOf (order[i]))
                return (int) i;
        return -1;
    };
    const int range = indexOf (v.rangeSelect());
    const int span = indexOf (v.spanSelect());
    const int pause = indexOf (v.pauseButton());
    const int list = indexOf (v.viewList());
    const int a = indexOf (v.compareSelect());
    const int b = indexOf (v.againstSelect());
    const int clash = indexOf (v.clashSwitch());
    const int picture = indexOf (rig.picture());
    const int motion = indexOf (frame.motionSwitch());
    CHECK (range >= 0);
    CHECK (range < span);
    CHECK (span < pause);
    CHECK (pause < list);
    CHECK (list < a);
    CHECK (a < b);
    CHECK (b < clash);
    CHECK (clash < picture);
    CHECK (picture < motion);
}

TEST_CASE ("spectrogram editor: no parameters -- nothing here changes what comes out")
{
    Rig rig;
    CHECK (rig.model.numParameters() == 0);
    rig.frame();
    CHECK (rig.model.base.params.events.empty());
}

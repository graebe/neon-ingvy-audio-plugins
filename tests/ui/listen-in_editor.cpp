// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In's window, held to its manual (plugins/listen-in/README.md and
 * docs/live.md): Bus picks one of sixteen and is the host's parameter; the
 * name is typed, kept on Enter or a click elsewhere, abandoned on Escape, and
 * is what the plugin kept, never more; the status says listening, slot taken,
 * unavailable or idle, and the line under it says why; the meter moves with
 * the input, greyed while it goes nowhere. Then the layout, the keyboard, the
 * scale, and each state as a picture.
 */
#include "ListenInEditor.h"

#include "InfoLines.h"
#include "listen-in_fakes.h"

#include "Pointer.h"
#include "checks.h"
#include "snapshot.h"

#include <doctest.h>

#include <cmath>

using ni::li::ListenInEditor;
using ni::li::Status;
using ni::li::test::FakeModel;
using ni::ui::Led;
using ni::ui::gallery::key;

namespace
{
/* An editor on a clock the test holds still, and a Ground that never asks the
 * system about reduced motion: the same pictures everywhere. */
struct Rig
{
    FakeModel model;
    double now = 0.0;   // the frame's clock, which a test moves
    ListenInEditor editor { model, [this] { return now; } };

    Rig() { editor.frame().ground().setReducedMotionQuery ([] { return false; }); }

    /* A part's bounds in the window's design pixels. */
    juce::Rectangle<int> inWindow (juce::Component& c)
    {
        return editor.frame().getLocalArea (&c, c.getLocalBounds());
    }

    juce::String line (juce::Component& c) { return ni::ui::infoOf (c); }

    /* What a press at `at` (window design pixels) lands on, by the window's
     * own hit testing -- and the point in its coordinates. */
    std::pair<juce::Component*, juce::Point<float>> hit (juce::Point<int> at)
    {
        auto* c = editor.frame().getComponentAt (at);
        REQUIRE (c != nullptr);
        return { c, c->getLocalPoint (&editor.frame(), at).toFloat() };
    }

    /* A press there, as JUCE gives it: to what it lands on, then to the
     * window, which hears every press. */
    void press (juce::Point<int> at, int clicks = 1)
    {
        auto [c, local] = hit (at);
        c->mouseDown (ni::ui::gallery::Pointer::event (*c, local, juce::ModifierKeys::leftButtonModifier, clicks));
        editor.frame().pressed (*c);
    }

    /* The Bus field's centre, in the window. */
    juce::Point<int> busField()
    {
        auto& bus = editor.busSelect();
        return editor.frame().getLocalPoint (&bus, bus.field().getCentre());
    }
};

/* Types `text` into `e` as keys, one character each. */
void type (juce::TextEditor& e, const juce::String& text)
{
    for (auto ch : text)
        e.keyPressed (juce::KeyPress (0, {}, ch));
}

/* An edit of the name field, as a person makes one: focus, select all, type,
 * then `end` (Enter, Escape) or a click elsewhere when `end` is 0. */
void typeName (ni::ui::TextField& f, const juce::String& text, int end = juce::KeyPress::returnKey)
{
    f.focusGained (juce::Component::focusChangedByMouseClick);
    f.selectAll();
    type (f, text);
    if (end != 0)
        key (f, end);
    f.focusLost (juce::Component::focusChangedByMouseClick);
}

const juce::String dash = juce::String::fromUTF8 (" \xe2\x80\x94 ");
} // namespace

/* ------------------------------------------------------------- layout -- */

TEST_CASE ("listen-in: 360 x 172, the bus and its name on top, the status and the meter under them")
{
    Rig rig;
    CHECK (rig.editor.getWidth() == 360);
    CHECK (rig.editor.getHeight() == 172);

    /* Row one: Bus at the padding, 28 + 8 + 64; the name field 16 on, to the edge. */
    CHECK (rig.inWindow (rig.editor.busSelect()) == juce::Rectangle<int> (32, 32, 100, 28));
    CHECK (rig.inWindow (rig.editor.nameField()) == juce::Rectangle<int> (148, 32, 180, 28));

    /* Row two, 16 under it: the LED in a fixed cell, the meter 16 on and
     * centred on the row. */
    const auto led = rig.inWindow (rig.editor.statusLed());
    CHECK (led.getPosition() == juce::Point<int> (32, 76));
    CHECK (led.getHeight() == 28);
    CHECK (led.getWidth() == Led::idealWidthFor ("unavailable"));
    const auto meter = rig.inWindow (rig.editor.levelMeter());
    CHECK (meter.getX() == led.getRight() + 16);
    CHECK (meter.getRight() == 328);
    CHECK (meter.getY() == 84);
    CHECK (meter.getHeight() == ni::ui::Meter::height);

    /* The bar on the bottom edge, 24 under the content. */
    CHECK (rig.editor.frame().hint().getBounds().getY() == 172 - 16 - 28);
    CHECK (rig.editor.frame().contentBounds().getBottom() == 104);
}

TEST_CASE ("listen-in: the status cell does not move the meter when the word changes")
{
    Rig rig;
    const auto before = rig.inWindow (rig.editor.levelMeter());
    for (auto s : { Status::live, Status::taken, Status::unavailable, Status::idle })
    {
        rig.model.state = s;
        rig.editor.refresh();
        CHECK (rig.inWindow (rig.editor.levelMeter()) == before);
    }
}

/* ---------------------------------------------------------------- bus -- */

TEST_CASE ("listen-in: Bus offers sixteen buses and shows the host parameter")
{
    Rig rig;
    auto& bus = rig.editor.busSelect();
    CHECK (bus.getOptions().size() == ni::li::numBuses);
    CHECK (bus.getOptions()[0] == "1");
    CHECK (bus.getOptions()[15] == "16");
    CHECK (bus.getIndex() == 0);

    /* Automation, a preset, another editor: the host's change shows. */
    rig.model.setBus (3);
    CHECK (bus.getIndex() == 2);
}

namespace
{
bool typed (ni::ui::Select& s, juce::juce_wchar ch, juce::ModifierKeys mods = {})
{
    return s.keyPressed (juce::KeyPress (ch, mods, ch));
}
} // namespace

TEST_CASE ("listen-in: typing a number chooses that bus, as the web editor's <select> did")
{
    SUBCASE ("one key")
    {
        Rig rig;
        CHECK (typed (rig.editor.busSelect(), '3'));
        CHECK (rig.model.bus() == 3);
        CHECK_FALSE (rig.editor.busSelect().isOpen());
    }

    SUBCASE ("two keys in a row are one number")
    {
        Rig rig;
        typed (rig.editor.busSelect(), '1');
        typed (rig.editor.busSelect(), '6');
        CHECK (rig.model.bus() == 16);
    }

    SUBCASE ("a pause starts a new one")
    {
        Rig rig;
        typed (rig.editor.busSelect(), '1');
        CHECK (rig.model.bus() == 10);   // after bus 1, the next to start with 1
        juce::Thread::sleep ((int) ni::ui::Select::typeAheadMs + 100);
        typed (rig.editor.busSelect(), '6');
        CHECK (rig.model.bus() == 6);
    }

    SUBCASE ("in the open list typing moves the highlight; Enter chooses it")
    {
        Rig rig;
        auto& bus = rig.editor.busSelect();
        key (bus, juce::KeyPress::downKey);
        REQUIRE (bus.isOpen());
        typed (bus, '7');
        CHECK (bus.getList()->getHighlighted() == 6);
        CHECK (rig.model.bus() == 1);
        key (bus, juce::KeyPress::returnKey);
        CHECK (rig.model.bus() == 7);
    }

    SUBCASE ("a shortcut is not typing: it is the host's")
    {
        Rig rig;
        CHECK_FALSE (typed (rig.editor.busSelect(), '2', juce::ModifierKeys::commandModifier));
        CHECK (rig.model.bus() == 1);
    }
}

TEST_CASE ("listen-in: a double-click on Bus puts the parameter's default back, wherever the list opened")
{
    Rig rig;
    auto& bus = rig.editor.busSelect();
    rig.model.setBus (5);
    rig.model.params.clear();

    /* The first click opens the list, which in this window covers the field;
     * the second lands on the list over it, and is the reset, not a row. */
    rig.press (rig.busField());
    REQUIRE (bus.isOpen());
    CHECK (rig.hit (rig.busField()).first == bus.getList());
    rig.press (rig.busField(), 2);

    CHECK_FALSE (bus.isOpen());
    CHECK (rig.model.bus() == 1);
    CHECK (rig.model.params.log() == "begin 0, value 0 0.000, end 0");

    /* One click on a row is still a choice. */
    rig.press (rig.busField());
    REQUIRE (bus.isOpen());
    auto* list = bus.getList();
    const auto row = list->rowBounds (list->getFirstShown() + 2).getCentre();
    ni::ui::gallery::Pointer().click (*list, row.toFloat());
    CHECK (rig.model.bus() == list->getFirstShown() + 3);
}

TEST_CASE ("listen-in: the Bus list shows six of its sixteen, the window's height (LI5, not 1.1.0)")
{
    /* The Select card allows 3 to 12 options; sixteen buses are the canvas's
     * proposed exception (LI5), and in a 172px window the list is as tall as
     * the window, over both rows and the bar, and scrolls. Pinned so a change
     * to either is a decision, not a side effect. */
    Rig rig;
    auto& bus = rig.editor.busSelect();
    key (bus, juce::KeyPress::downKey);
    REQUIRE (bus.isOpen());
    auto* list = bus.getList();

    CHECK (list->getNumRows() == 16);
    CHECK (list->numShown() == 6);
    const auto field = rig.editor.frame().getLocalArea (&bus, bus.field());
    CHECK (rig.inWindow (*list) == juce::Rectangle<int> (field.getX(), 0, field.getWidth(), 172));

    key (bus, juce::KeyPress::endKey);
    CHECK (list->getFirstShown() == 10);
}

TEST_CASE ("listen-in: under the pointer Bus and Name rise to bg-300 with an ink-dim hairline")
{
    Rig rig;
    auto& bus = rig.editor.busSelect();
    auto& name = rig.editor.nameField();
    namespace c = uv::tok::colour;

    const auto edges = [&]
    {
        const auto img = ni::ui::test::render (rig.editor);
        const auto b = rig.inWindow (bus).getX() + bus.field().getX();
        const auto n = rig.inWindow (name);
        return std::pair { img.getPixelAt (b, 46), img.getPixelAt (n.getX(), 46) };
    };
    const auto wells = [&]
    {
        const auto img = ni::ui::test::render (rig.editor);
        const auto b = rig.inWindow (bus).getX() + bus.field().getX();
        return std::pair { img.getPixelAt (b + 4, 34), img.getPixelAt (rig.inWindow (name).getRight() - 4, 34) };
    };

    CHECK (edges().first == c::line200);
    CHECK (edges().second == c::line200);
    CHECK (wells().first == c::bg200);
    CHECK (wells().second == c::bg200);

    ni::ui::gallery::Pointer p;
    p.enter (bus, bus.field().getCentre().toFloat());
    p.enter (name, { 20.0f, 14.0f });
    CHECK (bus.isFieldHovered());
    CHECK (name.isHovered());
    CHECK (edges().first == c::inkDim);
    CHECK (edges().second == c::inkDim);
    CHECK (wells().first == c::bg300);
    CHECK (wells().second == c::bg300);

    /* Typed into, the field is the edit's: uv, its well bg-200. */
    name.focusGained (juce::Component::focusChangedByMouseClick);
    CHECK (edges().second == c::uv);
    CHECK (wells().second == c::bg200);
    name.focusLost (juce::Component::focusChangedByMouseClick);

    p.exit (bus);
    p.exit (name);
    CHECK (edges().first == c::line200);
    CHECK (edges().second == c::line200);
}

TEST_CASE ("listen-in: choosing a bus from the keyboard is one gesture on the host parameter")
{
    Rig rig;
    auto& bus = rig.editor.busSelect();
    rig.model.params.clear();

    key (bus, juce::KeyPress::downKey);   // opens the list on the current bus
    REQUIRE (bus.isOpen());
    key (bus, juce::KeyPress::downKey);
    key (bus, juce::KeyPress::downKey);
    key (bus, juce::KeyPress::returnKey);
    CHECK_FALSE (bus.isOpen());

    CHECK (rig.model.bus() == 3);
    CHECK (rig.model.params.log() == "begin 0, value 0 0.133, end 0");
}

/* --------------------------------------------------------------- name -- */

TEST_CASE ("listen-in: an empty name shows the placeholder, never \"(null)\"")
{
    Rig rig;
    auto& name = rig.editor.nameField();
    CHECK (rig.model.name.isEmpty());
    CHECK (name.getText().isEmpty());
    CHECK (name.getValue().isEmpty());
    CHECK (name.getTitle() == "Bus name");

    /* The window's lines for a bus with no name say so in words. */
    rig.model.state = Status::live;
    rig.editor.refresh();
    CHECK (rig.line (rig.editor.statusLed()) == "Status" + dash + "listening on bus 1, unnamed.");
    for (const auto& line : ni::ui::collectInfo (rig.editor))
        CHECK_FALSE (line.contains ("null"));

    /* The placeholder is drawn: ink-muted text where the field is empty. */
    const auto img = ni::ui::test::render (rig.editor);
    const auto field = rig.inWindow (name);
    int muted = 0;
    for (int x = field.getX() + 9; x < field.getX() + 100; ++x)
        for (int y = field.getY() + 8; y < field.getBottom() - 8; ++y)
            if (img.getPixelAt (x, y).getBrightness() > uv::tok::colour::bg200.getBrightness() + 0.2f)
                ++muted;
    CHECK (muted > 20);
}

TEST_CASE ("listen-in: Enter keeps the name, once per edit, and the field shows what the plugin kept")
{
    Rig rig;
    auto& name = rig.editor.nameField();
    name.focusGained (juce::Component::focusChangedByMouseClick);
    name.selectAll();
    type (name, "kick bus");
    CHECK (rig.model.typedLabels.isEmpty());   // never per keystroke

    key (name, juce::KeyPress::returnKey);
    name.focusLost (juce::Component::focusChangedDirectly);
    CHECK (rig.model.typedLabels == juce::StringArray { "kick bus" });
    CHECK (rig.model.name == "kick bus");
    CHECK (name.getText() == "kick bus");
}

TEST_CASE ("listen-in: a click elsewhere keeps the name too; Escape abandons it")
{
    Rig rig;
    auto& name = rig.editor.nameField();

    typeName (name, "pad", 0);
    CHECK (rig.model.name == "pad");

    typeName (name, "lead", juce::KeyPress::escapeKey);
    CHECK (rig.model.typedLabels == juce::StringArray { "pad" });
    CHECK (name.getText() == "pad");
}

TEST_CASE ("listen-in: a press anywhere else in the window keeps the name -- the background, the LED, the meter, the bar")
{
    Rig rig;
    auto& name = rig.editor.nameField();
    auto& frame = rig.editor.frame();

    /* Places nothing takes the keyboard from: the content between the rows,
     * the LED, the meter, the hint's text. */
    const auto led = rig.inWindow (rig.editor.statusLed()).getCentre();
    const auto meter = rig.inWindow (rig.editor.levelMeter()).getCentre();
    const auto tips = frame.getLocalArea (&frame.hint(), frame.hint().tipsBounds()).getCentre();
    const juce::Point<int> between { 200, 68 };

    int n = 0;
    for (auto at : { between, led, meter, tips })
    {
        CAPTURE (at.toString());
        const auto typed = "name " + juce::String (++n);
        name.focusGained (juce::Component::focusChangedByMouseClick);
        name.selectAll();
        type (name, typed);
        REQUIRE (name.isBeingEdited());

        rig.press (at);
        CHECK_FALSE (name.isBeingEdited());
        CHECK (rig.model.name == typed);
        name.focusLost (juce::Component::focusChangedDirectly);   // what JUCE then sends
    }
    CHECK (rig.model.typedLabels.size() == 4);

    /* A press on the field itself is the edit's own: it goes on. */
    name.focusGained (juce::Component::focusChangedByMouseClick);
    type (name, "x");
    rig.press (rig.inWindow (name).getCentre());
    CHECK (name.isBeingEdited());
}

TEST_CASE ("listen-in: Tab into the name selects it and shows its line; typing replaces it")
{
    Rig rig;
    auto& name = rig.editor.nameField();
    auto& frame = rig.editor.frame();
    rig.model.name = "kick";
    rig.editor.refresh();

    name.focusGained (juce::Component::focusChangedByTabKey);
    CHECK (frame.infoState().text() == ni::li::info::name.str());
    CHECK (name.getHighlightedText() == "kick");

    type (name, "bass");
    key (name, juce::KeyPress::returnKey);
    name.focusLost (juce::Component::focusChangedDirectly);
    CHECK (rig.model.name == "bass");
}

TEST_CASE ("listen-in: a field being typed into shows its line however it was focused, and drops it after")
{
    Rig rig;
    auto& name = rig.editor.nameField();
    auto& frame = rig.editor.frame();

    /* :focus-visible matches a text input focused by the pointer too. */
    name.focusGained (juce::Component::focusChangedByMouseClick);
    CHECK (frame.infoState().text() == ni::li::info::name.str());

    name.focusLost (juce::Component::focusChangedByMouseClick);
    rig.now += 1000.0;   // past the grace
    frame.poll();
    CHECK_FALSE (frame.hint().content().info.has_value());
}

TEST_CASE ("listen-in: a colon is dropped and a long name cut on a character, as the plugin keeps them")
{
    Rig rig;
    auto& name = rig.editor.nameField();

    /* "a:b" is typed and "ab" kept -- and the field says so, though the name
     * before was "ab" too. */
    rig.model.name = "ab";
    rig.editor.refresh();
    typeName (name, "a:b");
    CHECK (rig.model.typedLabels == juce::StringArray { "a:b" });
    CHECK (name.getText() == "ab");

    /* Thirty-one characters, but 32 bytes: the umlaut at the end does not fit
     * whole, and half of it would be no character at all. */
    const auto long_ = juce::String::repeatedString ("x", 30) + juce::String::fromUTF8 ("\xc3\xa4");
    CHECK (long_.length() == 31);
    CHECK (long_.getNumBytesAsUTF8() == 32);
    typeName (name, long_);
    CHECK (name.getText() == long_.dropLastCharacters (1));
    CHECK (name.getText().getNumBytesAsUTF8() <= ni::li::maxLabelBytes);
}

TEST_CASE ("listen-in: the field never takes more than 31 characters")
{
    Rig rig;
    auto& name = rig.editor.nameField();
    name.focusGained (juce::Component::focusChangedByMouseClick);
    type (name, juce::String::repeatedString ("x", 40));
    CHECK (name.getText().length() == ni::li::maxLabelBytes);
}

TEST_CASE ("listen-in: a name loaded with the set shows at once; one being typed is left alone")
{
    Rig rig;
    auto& name = rig.editor.nameField();

    rig.model.name = juce::String::fromUTF8 ("B\xc3\xa4sse & Kick");
    rig.editor.refresh();
    CHECK (name.getText() == rig.model.name);

    name.focusGained (juce::Component::focusChangedByMouseClick);
    name.selectAll();
    type (name, "hats");
    rig.model.name = "snare";
    rig.editor.refresh();
    CHECK (name.getText() == "hats");

    /* Escape goes back to the plugin's name -- the one it holds now. */
    key (name, juce::KeyPress::escapeKey);
    CHECK (name.getText() == "snare");
}

/* ------------------------------------------------------------- status -- */

TEST_CASE ("listen-in: the LED says the status in its word and its colour, and the line says why")
{
    Rig rig;
    rig.model.setBus (3);
    rig.model.name = "kick bus";
    auto& led = rig.editor.statusLed();

    struct Case
    {
        Status status;
        Led::Status lens;
        const char* word;
        const char* why;
    };
    const Case cases[] {
        { Status::idle, Led::Status::off, "idle", "starting: no audio yet; press play." },
        { Status::live, Led::Status::on, "listening", "listening on bus 3, named kick bus." },
        { Status::taken, Led::Status::warn, "slot taken", "bus 3 is taken: another Listen-In holds it." },
        { Status::unavailable, Led::Status::clip, "unavailable", "bus unavailable: the host may be sandboxed." },
    };

    for (const auto& c : cases)
    {
        CAPTURE (c.word);
        rig.model.state = c.status;
        rig.editor.refresh();
        CHECK (led.getStatus() == c.lens);
        CHECK (led.getLabel() == c.word);
        CHECK (rig.line (led) == "Status" + dash + c.why);
        NI_CHECK_INFO_LIMIT (rig.editor);
    }
}

TEST_CASE ("listen-in: the line follows the bus and the name")
{
    Rig rig;
    rig.model.state = Status::taken;
    rig.model.setBus (12);
    rig.editor.refresh();
    CHECK (rig.line (rig.editor.statusLed()).contains ("bus 12 is taken"));

    rig.model.state = Status::live;
    rig.model.name = "pad";
    rig.editor.refresh();
    CHECK (rig.line (rig.editor.statusLed()).endsWith ("bus 12, named pad."));
}

TEST_CASE ("listen-in: the longest line -- bus 16 under a 31-character name -- is within 72")
{
    Rig rig;
    rig.model.setBus (16);
    rig.model.state = Status::live;
    rig.model.name = juce::String::repeatedString ("W", ni::li::maxLabelBytes);
    rig.editor.refresh();
    CHECK (rig.line (rig.editor.statusLed()).length() == 68);
    NI_CHECK_INFO_LIMIT (rig.editor);
}

TEST_CASE ("listen-in: under the pointer, the bar shows the status line, and follows a change")
{
    Rig rig;
    auto& frame = rig.editor.frame();
    auto& led = rig.editor.statusLed();
    rig.model.state = Status::taken;
    rig.model.setBus (3);
    rig.editor.refresh();

    /* At rest, the conventions. */
    const std::vector<ni::ui::Clause> conventions { { "pick", "a bus" } };
    CHECK (frame.hint().getConventions() == conventions);
    CHECK_FALSE (frame.hint().content().info.has_value());

    CHECK (ni::ui::infoSource (&led) == &led);
    frame.infoState().enter (rig.line (led), &led);
    REQUIRE (frame.hint().content().info.has_value());
    CHECK (frame.hint().content().info->name == "Status");
    CHECK (frame.hint().content().info->rest.contains ("bus 3 is taken"));

    /* Another bus was freed, the claim went through: the bar says so as the
     * pointer rests there. */
    rig.model.state = Status::live;
    rig.editor.refresh();
    REQUIRE (frame.hint().content().info.has_value());
    CHECK (frame.hint().content().info->rest.contains ("listening on bus 3"));
}

TEST_CASE ("listen-in: the tips take the bar to Motion, which sits space-4 before the Signature")
{
    Rig rig;
    auto& bar = rig.editor.frame().hint();
    CHECK (ni::ui::clausesWidth (bar.getConventions()) <= (float) bar.tipsBounds().getWidth());
    CHECK (bar.tipsBounds().getRight() + ni::ui::Hint::gap == bar.motionBounds().getX());
    CHECK (bar.motionBounds().getRight() + ni::ui::Hint::gap == bar.signature().getX());
}

TEST_CASE ("listen-in: every control has its line, within the limit")
{
    Rig rig;
    CHECK (rig.line (rig.editor.busSelect()).startsWith ("Bus"));
    CHECK (rig.line (rig.editor.nameField()).startsWith ("Name"));
    CHECK (rig.line (rig.editor.levelMeter()).startsWith ("Level"));
    CHECK (rig.line (rig.editor.statusLed()).startsWith ("Status"));
    CHECK (rig.line (rig.editor.frame().motionSwitch()) == ni::ui::EditorFrame::motionInfo.str());
    CHECK (rig.line (rig.editor.frame().hint().signature()).startsWith ("Neon Ingvy"));
    NI_CHECK_INFO_LIMIT (rig.editor);

    /* The meter is found by the pointer for its line, though it takes no
     * press of its own. */
    bool self = false, children = true;
    rig.editor.levelMeter().getInterceptsMouseClicks (self, children);
    CHECK (self);
}

/* -------------------------------------------------------------- meter -- */

TEST_CASE ("listen-in: the meter is linear in dB from -60 to 0")
{
    using ni::li::meterFraction;
    CHECK (meterFraction (0.0f) == 0.0f);
    CHECK (meterFraction (-1.0f) == 0.0f);
    CHECK (meterFraction (std::nanf ("")) == 0.0f);
    CHECK (meterFraction (0.001f) == 0.0f);                          // -60 dB, the floor
    CHECK (meterFraction (0.1f) == doctest::Approx (2.0 / 3.0));    // -20 dB
    CHECK (meterFraction (0.5f) == doctest::Approx (1.0 + 20.0 * std::log10 (0.5) / 60.0));
    CHECK (meterFraction (1.0f) == 1.0f);
    CHECK (meterFraction (1.5f) == 1.0f);
}

TEST_CASE ("listen-in: the meter shows the peak, live in uv only while the bus is")
{
    Rig rig;
    auto& meter = rig.editor.levelMeter();
    rig.model.level = 0.1f;

    rig.model.state = Status::live;
    rig.editor.refresh();
    CHECK (meter.getLevel() == doctest::Approx (2.0 / 3.0));
    CHECK (meter.isLive());

    /* Audio on the track, going nowhere: still measured, greyed. */
    for (auto s : { Status::taken, Status::unavailable, Status::idle })
    {
        rig.model.state = s;
        rig.editor.refresh();
        CHECK_FALSE (meter.isLive());
        CHECK (meter.getLevel() == doctest::Approx (2.0 / 3.0));
    }
}

/* ------------------------------------------------------ the window's own -- */

TEST_CASE ("listen-in: the frame clock reads the model; nothing else is needed for it to be right")
{
    Rig rig;
    rig.model.state = Status::live;
    rig.model.level = 1.0f;
    CHECK (rig.editor.statusLed().getStatus() == Led::Status::off);

    rig.editor.frame().frameClock().tick();
    CHECK (rig.editor.statusLed().getStatus() == Led::Status::on);
    CHECK (rig.editor.levelMeter().getLevel() == 1.0f);
    CHECK (rig.editor.frame().frameClock().isRunning());
}

TEST_CASE ("listen-in: Motion is the model's, and the Ground follows it")
{
    FakeModel model;
    model.motionOn = false;
    ListenInEditor editor (model, [] { return 0.0; });
    CHECK_FALSE (editor.frame().motionSwitch().isOn());
    CHECK_FALSE (editor.frame().ground().isEnabled());

    ni::ui::gallery::Pointer().click (editor.frame().motionSwitch(), { 7.0f, 14.0f });
    CHECK (model.motionOn);
    CHECK (editor.frame().ground().isEnabled());
}

TEST_CASE ("listen-in: Tab goes Bus, then the name, and the meter and the LED take no focus")
{
    Rig rig;
    auto& frame = rig.editor.frame();
    CHECK (frame.isKeyboardFocusContainer());

    auto traverser = frame.createKeyboardFocusTraverser();
    REQUIRE (traverser != nullptr);
    CHECK (traverser->getDefaultComponent (&frame) == &rig.editor.busSelect());
    CHECK (traverser->getNextComponent (&rig.editor.busSelect()) == &rig.editor.nameField());

    const auto all = traverser->getAllComponents (&frame);
    CHECK (std::find (all.begin(), all.end(), &rig.editor.statusLed()) == all.end());
    CHECK (std::find (all.begin(), all.end(), &rig.editor.levelMeter()) == all.end());
}

TEST_CASE ("listen-in: the design scales to the host's size and keeps its proportion")
{
    Rig rig;
    rig.editor.setSize (720, 344);
    CHECK (rig.editor.design().getScale() == doctest::Approx (2.0f));
    CHECK (rig.editor.frame().getWidth() == 360);   // laid out at its design size, always

    juce::ComponentBoundsConstrainer c;
    ListenInEditor::constrain (c);
    CHECK (c.getFixedAspectRatio() == doctest::Approx (360.0 / 172.0));
}

/* ---------------------------------------------------------- pictures -- */

namespace
{
void picture (Rig& rig, Status s, float peak, const juce::String& name, int bus)
{
    rig.model.state = s;
    rig.model.level = peak;
    rig.model.name = name;
    rig.model.setBus (bus);
    rig.editor.refresh();
    NI_CHECK_INFO_LIMIT (rig.editor);
}
} // namespace

NI_SNAPSHOT_TEST ("listen-in: listening on bus 3 as kick bus")
{
    Rig rig;
    picture (rig, Status::live, 0.4271f, "kick bus", 3);
    NI_CHECK_SNAPSHOT (rig.editor, "listen-in-live");
}

NI_SNAPSHOT_TEST ("listen-in: slot taken, the input greyed")
{
    Rig rig;
    picture (rig, Status::taken, 0.2f, "bass", 3);
    NI_CHECK_SNAPSHOT (rig.editor, "listen-in-taken");
}

NI_SNAPSHOT_TEST ("listen-in: unavailable")
{
    Rig rig;
    picture (rig, Status::unavailable, 0.3f, "pad", 7);
    NI_CHECK_SNAPSHOT (rig.editor, "listen-in-unavailable");
}

NI_SNAPSHOT_TEST ("listen-in: idle with no name, the placeholder showing")
{
    Rig rig;
    picture (rig, Status::idle, 0.0f, {}, 1);
    NI_CHECK_SNAPSHOT (rig.editor, "listen-in-idle");
}

NI_SNAPSHOT_TEST ("listen-in: the name being typed, and the status line in the bar")
{
    Rig rig;
    picture (rig, Status::live, 0.05f, "kick", 2);
    rig.editor.nameField().focusGained (juce::Component::focusChangedByMouseClick);
    rig.editor.frame().infoState().enter (rig.line (rig.editor.statusLed()), &rig.editor.statusLed());
    NI_CHECK_SNAPSHOT (rig.editor, "listen-in-editing");
}

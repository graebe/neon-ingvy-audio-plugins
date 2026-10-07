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
    ListenInEditor editor { model, [] { return 0.0; } };

    Rig() { editor.frame().ground().setReducedMotionQuery ([] { return false; }); }

    /* A part's bounds in the window's design pixels. */
    juce::Rectangle<int> inWindow (juce::Component& c)
    {
        return editor.frame().getLocalArea (&c, c.getLocalBounds());
    }

    juce::String line (juce::Component& c) { return ni::ui::infoOf (c); }
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

TEST_CASE ("listen-in: the conventions fit whole beside Motion and the Signature")
{
    Rig rig;
    auto& bar = rig.editor.frame().hint();
    CHECK (ni::ui::clausesWidth (bar.getConventions()) <= (float) bar.tipsBounds().getWidth());
    CHECK (bar.motionBounds().getX() >= bar.tipsBounds().getRight() + ni::ui::Hint::gap);
    CHECK (bar.signature().getX() >= bar.motionBounds().getRight() + ni::ui::Hint::gap);
}

TEST_CASE ("listen-in: every control has its line, within the limit")
{
    Rig rig;
    CHECK (rig.line (rig.editor.busSelect()).startsWith ("Bus"));
    CHECK (rig.line (rig.editor.nameField()).startsWith ("Name"));
    CHECK (rig.line (rig.editor.levelMeter()).startsWith ("Level"));
    CHECK (rig.line (rig.editor.statusLed()).startsWith ("Status"));
    CHECK (rig.line (rig.editor.frame().motionSwitch()).startsWith ("Motion"));
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

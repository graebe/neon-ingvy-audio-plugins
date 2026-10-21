// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The RadialMenu: its ring round the point and kept inside the window, what
 * is its to hit and what is left to the owner, a value item's drag as a
 * Knob's (the dead zone, the travel, Shift finer or the alternate, the
 * detents), a click on an action, a double-click's reset, every key, the
 * hint it names, a gesture kept across setItems, and what a screen reader
 * hears. Then its gallery page, as a picture.
 */
#include "Detents.h"
#include "Gallery.h"
#include "Pointer.h"
#include "RadialMenu.h"
#include "checks.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::RadialMenu;
using ni::ui::gallery::Pointer;
using Item = RadialMenu::Item;

namespace
{
Item value (const char* id, double v)
{
    Item it;
    it.id = id;
    it.label = id;
    it.value01 = v;
    it.valueText = juce::String (juce::roundToInt (v * 100.0)) + " %";
    return it;
}

Item action (const char* id)
{
    Item it;
    it.id = id;
    it.label = id;
    it.kind = Item::Kind::action;
    it.icon = "copy";
    return it;
}

/* Four items: two values, one with Shift as its alternate, then an action;
 * placed in the middle of an 800 x 600 window. */
struct Recorded
{
    Recorded()
    {
        auto depth = value ("depth", 0.5);
        auto glide = value ("glide", 0.4);
        glide.shiftIsAlternate = true;
        glide.alternate01 = 0.2;
        auto level = value ("level", 0.4);
        level.detents = { 0.5 };
        items = { depth, glide, level, action ("copy") };
        menu.setItems (items);
        menu.setCentreText ("3 notes");
        window.setBounds (0, 0, 800, 600);
        window.addAndMakeVisible (menu);
        menu.placeAround ({ 400, 300 }, window.getLocalBounds());

        menu.onBegin = [this] (const juce::String& id, bool alt) { log.add ("begin " + id + (alt ? " alt" : "")); };
        menu.onInput = [this] (const juce::String& id, double v, bool alt) {
            log.add ("input " + id + " " + juce::String (v, 3) + (alt ? " alt" : ""));
        };
        menu.onEnd = [this] (const juce::String& id) { log.add ("end " + id); };
        menu.onReset = [this] (const juce::String& id) { log.add ("reset " + id); };
        menu.onAction = [this] (const juce::String& id) { log.add ("action " + id); };
        menu.onHint = [this] (const juce::String& id) { log.add ("hint " + id); };
        menu.onClose = [this] { log.add ("close"); };
    }

    juce::String said() { const auto s = log.joinIntoString (", "); log.clear(); return s; }

    /* Item i's centre, in its own component's coordinates. */
    juce::Point<float> at (int i) { return menu.itemCentre (i) - menu.item (i).getPosition().toFloat(); }

    bool key (int i, int code, juce::ModifierKeys mods = {})
    {
        return menu.item (i).keyPressed (juce::KeyPress (code, mods, 0));
    }

    juce::Component window;
    RadialMenu menu;
    std::vector<Item> items;
    juce::StringArray log;
};

const auto shift = juce::ModifierKeys (juce::ModifierKeys::shiftModifier);
} // namespace

/* ------------------------------------------------------------- the ring */

TEST_CASE ("radial menu: the items round a 56px ring, the first at 12 o'clock, clockwise")
{
    Recorded r;
    CHECK (r.menu.getCentrePoint() == juce::Point<int> (400, 300));
    const auto centre = r.menu.getCentrePoint().toFloat() - r.menu.getPosition().toFloat();
    const juce::Point<float> expected[] = { { 0.0f, -56.0f }, { 56.0f, 0.0f }, { 0.0f, 56.0f }, { -56.0f, 0.0f } };
    for (int i = 0; i < 4; ++i)
    {
        CAPTURE (i);
        CHECK (r.menu.itemCentre (i).x == doctest::Approx (centre.x + expected[i].x).epsilon (1e-4));
        CHECK (r.menu.itemCentre (i).y == doctest::Approx (centre.y + expected[i].y).epsilon (1e-4));
        /* Each label outside the plate, along the item's direction. */
        const auto chip = r.menu.labelBounds (i);
        CHECK_FALSE (chip.isEmpty());
        CHECK (chip.getCentre().getDistanceFrom (centre) > RadialMenu::plateRadius);
    }
    CHECK (r.menu.labelBounds (1).getX() == doctest::Approx (centre.x + RadialMenu::plateRadius + RadialMenu::labelGap));
    CHECK (r.menu.labelBounds (3).getRight() == doctest::Approx (centre.x - RadialMenu::plateRadius - RadialMenu::labelGap));
    CHECK (r.menu.labelBounds (0).getBottom() == doctest::Approx (centre.y - RadialMenu::plateRadius - RadialMenu::labelGap));

    /* The menu covers the plate and the labels, and its light past them. */
    CHECK (r.menu.getBounds() == r.menu.footprint().expanded (RadialMenu::lightMargin));
    CHECK (RadialMenu::plateRadius == doctest::Approx (84.0));
}

TEST_CASE ("radial menu: placed at a window's edge or corner, it moves just inside")
{
    Recorded r;
    const auto window = r.window.getLocalBounds();
    for (const auto at : { juce::Point<int> (0, 0), juce::Point<int> (800, 600), juce::Point<int> (800, 0),
                           juce::Point<int> (0, 600), juce::Point<int> (400, 2), juce::Point<int> (3, 300) })
    {
        CAPTURE (at.toString());
        r.menu.placeAround (at, window);
        const auto f = r.menu.footprint();
        CHECK (window.contains (f));
        /* As near the point as fits: touching the edge it was pushed from. */
        CHECK ((f.getX() == 0 || f.getRight() == 800 || r.menu.getCentrePoint().x == at.x));
        CHECK ((f.getY() == 0 || f.getBottom() == 600 || r.menu.getCentrePoint().y == at.y));
    }

    /* Room enough: exactly where asked. */
    r.menu.placeAround ({ 420, 280 }, window);
    CHECK (r.menu.getCentrePoint() == juce::Point<int> (420, 280));

    /* Too small a window: centred on it. */
    const juce::Rectangle<int> tiny { 100, 100, 120, 80 };
    r.menu.placeAround ({ 0, 0 }, tiny);
    CHECK (std::abs (r.menu.footprint().getCentreX() - tiny.getCentreX()) <= 1);
    CHECK (std::abs (r.menu.footprint().getCentreY() - tiny.getCentreY()) <= 1);

    /* New items -- wider labels -- are placed again inside. */
    r.menu.placeAround ({ 799, 300 }, window);
    auto wide = r.items;
    wide[1].label = "a much longer label";
    r.menu.setItems (wide);
    CHECK (window.contains (r.menu.footprint()));
}

TEST_CASE ("radial menu: the plate, the discs and the labels are its; everything else is the owner's")
{
    Recorded r;
    const auto centre = (r.menu.getCentrePoint() - r.menu.getPosition());
    CHECK (r.menu.hitTest (centre.x, centre.y));
    CHECK (r.menu.hitTest (centre.x + 80, centre.y - 10));   // on the plate
    CHECK_FALSE (r.menu.hitTest (2, 2));                     // its corner: light only
    CHECK_FALSE (r.menu.hitTest (centre.x + 90, centre.y - 90));

    for (int i = 0; i < 4; ++i)
    {
        CAPTURE (i);
        CHECK (r.menu.itemAt (r.menu.itemCentre (i)) == i);
        CHECK (r.menu.itemAt (r.menu.labelBounds (i).getCentre()) == i);
        const auto chip = r.menu.labelBounds (i).getCentre().roundToInt();
        CHECK (r.menu.hitTest (chip.x, chip.y));
        /* An item's component answers for its disc and label only. */
        const auto local = r.at (i).roundToInt();
        CHECK (r.menu.item (i).hitTest (local.x, local.y));
    }
    CHECK (r.menu.itemAt (centre.toFloat()) == -1);
    CHECK (r.menu.itemAt (r.menu.itemCentre (0) + juce::Point<float> (0.0f, RadialMenu::railRadius + 1.0f)) == -1);
}

/* ------------------------------------------------------------ the pointer */

TEST_CASE ("radial menu: a drag does nothing inside 4px, then opens its gesture and runs 200px for the range")
{
    Recorded r;
    Pointer p;
    const auto c = r.at (0);
    p.down (r.menu.item (0), c);
    p.drag (c + juce::Point<float> (3.0f, -3.0f));
    CHECK (r.said() == "");
    CHECK (r.menu.getHot() == -1);
    p.drag (c + juce::Point<float> (0.0f, -10.0f));
    CHECK (r.said() == "hint depth, begin depth, input depth 0.550");
    CHECK (r.menu.getHot() == 0);
    p.drag (c + juce::Point<float> (0.0f, 40.0f));
    p.drag (c + juce::Point<float> (0.0f, -200.0f));
    CHECK (r.said() == "input depth 0.300, input depth 1.000");
    p.up();
    CHECK (r.said() == "hint , end depth");
    CHECK (r.menu.getHot() == -1);
}

TEST_CASE ("radial menu: Shift is five times finer -- or, on an alternate item, its other parameter")
{
    Recorded r;
    Pointer p;
    p.down (r.menu.item (0), r.at (0), shift);
    p.drag (r.at (0) + juce::Point<float> (0.0f, -50.0f));
    p.up();
    CHECK (r.said() == "hint depth, begin depth, input depth 0.550, hint , end depth");

    /* From its alternate's value, 200px for the range, alternate said. */
    p.down (r.menu.item (1), r.at (1), shift);
    p.drag (r.at (1) + juce::Point<float> (0.0f, -20.0f));
    p.up();
    CHECK (r.said() == "hint glide, begin glide alt, input glide 0.300 alt, hint , end glide");

    /* Without Shift, the value itself. */
    p.down (r.menu.item (1), r.at (1));
    p.drag (r.at (1) + juce::Point<float> (0.0f, -20.0f));
    p.up();
    CHECK (r.said() == "hint glide, begin glide, input glide 0.500, hint , end glide");
}

TEST_CASE ("radial menu: a detent holds for 14px; Shift passes it")
{
    Recorded r;
    Pointer p;
    const auto c = r.at (2);
    p.down (r.menu.item (2), c);
    for (const float dy : { 10.0f, 20.0f, 30.0f, 33.0f, 40.0f })
        p.drag (c - juce::Point<float> (0.0f, dy));
    p.up();
    CHECK (r.said()
           == "hint level, begin level, input level 0.450, input level 0.500, input level 0.500, "
              "input level 0.500, input level 0.530, hint , end level");
    CHECK (ni::ui::detents::dragValue (0.4, 30.0, 200.0, { 0.5 }) == doctest::Approx (0.5));

    p.down (r.menu.item (2), c, shift);
    p.drag (c - juce::Point<float> (0.0f, 100.0f));
    p.up();
    CHECK (r.said() == "hint level, begin level, input level 0.500, hint , end level");
}

TEST_CASE ("radial menu: a click on an action acts, a drag off it does not; a click on a value is no edit")
{
    Recorded r;
    Pointer p;
    p.click (r.menu.item (3), r.at (3));
    CHECK (r.said() == "action copy");

    p.down (r.menu.item (3), r.at (3));
    p.drag (r.at (3) + juce::Point<float> (0.0f, -12.0f));
    p.up();
    CHECK (r.said() == "");

    /* Released off it without a drag: no click on it. */
    p.down (r.menu.item (3), r.at (3));
    p.up (r.at (3) + juce::Point<float> (60.0f, 0.0f));
    CHECK (r.said() == "");

    p.click (r.menu.item (0), r.at (0));
    CHECK (r.said() == "");

    /* The right button is not the menu's. */
    const auto right = juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier);
    for (const int i : { 0, 3 })
    {
        auto& item = r.menu.item (i);
        item.mouseDown (Pointer::event (item, r.at (i), right, 1, r.at (i)));
        item.mouseDrag (Pointer::event (item, r.at (i) - juce::Point<float> (0.0f, 30.0f), right, 1, r.at (i), true));
        item.mouseUp (Pointer::event (item, r.at (i), {}, 1, r.at (i), true));
    }
    CHECK (r.said() == "");
}

TEST_CASE ("radial menu: a double-click resets a value item, and nothing else")
{
    Recorded r;
    Pointer p;
    p.doubleClick (r.menu.item (0), r.at (0));
    CHECK (r.said() == "reset depth");
    p.doubleClick (r.menu.item (3), r.at (3));
    CHECK (r.said() == "action copy, action copy");
}

TEST_CASE ("radial menu: a disabled item takes nothing, and the keyboard passes it by")
{
    Recorded r;
    auto items = r.items;
    items[1].enabled = false;
    items[3].enabled = false;
    r.menu.setItems (items);
    r.said();

    Pointer p;
    p.down (r.menu.item (1), r.at (1));
    p.drag (r.at (1) - juce::Point<float> (0.0f, 30.0f));
    p.up();
    p.click (r.menu.item (3), r.at (3));
    p.enter (r.menu.item (1), r.at (1));
    CHECK (r.said() == "");
    CHECK_FALSE (r.key (1, juce::KeyPress::upKey));
    CHECK_FALSE (r.key (3, juce::KeyPress::returnKey));
    CHECK_FALSE ((r.menu.item (1).getWantsKeyboardFocus() && r.menu.item (1).isEnabled()));

    CHECK (r.key (0, juce::KeyPress::rightKey));
    CHECK (r.menu.getKeyboardItem() == 2);
    CHECK (r.key (2, juce::KeyPress::rightKey));
    CHECK (r.menu.getKeyboardItem() == 0);

    /* The whole menu disabled: nothing at all. */
    r.menu.setItems (r.items);
    r.menu.setEnabled (false);
    r.said();
    p.click (r.menu.item (3), r.at (3));
    CHECK_FALSE (r.key (0, juce::KeyPress::upKey));
    CHECK (r.said() == "");
}

/* ----------------------------------------------------------- the keyboard */

TEST_CASE ("radial menu: Up and Down step a value 1 %, Shift 0.2 % or the alternate; Page and Home/End")
{
    Recorded r;
    CHECK (r.key (0, juce::KeyPress::upKey));
    CHECK (r.said() == "begin depth, input depth 0.510, end depth");
    CHECK (r.key (0, juce::KeyPress::downKey, shift));
    CHECK (r.said() == "begin depth, input depth 0.498, end depth");
    CHECK (r.key (1, juce::KeyPress::upKey, shift));
    CHECK (r.said() == "begin glide alt, input glide 0.210 alt, end glide");
    CHECK (r.key (2, juce::KeyPress::pageUpKey));
    CHECK (r.said() == "begin level, input level 0.500, end level");
    CHECK (r.key (2, juce::KeyPress::pageDownKey));
    CHECK (r.said() == "begin level, input level 0.300, end level");
    CHECK (r.key (0, juce::KeyPress::endKey));
    CHECK (r.said() == "begin depth, input depth 1.000, end depth");
    CHECK (r.key (0, juce::KeyPress::homeKey));
    CHECK (r.said() == "begin depth, input depth 0.000, end depth");

    /* An action has no value. */
    CHECK_FALSE (r.key (3, juce::KeyPress::upKey));
    CHECK (r.said() == "");
}

TEST_CASE ("radial menu: Tab and the arrows go round, Enter acts, Escape asks to close")
{
    Recorded r;
    CHECK (r.key (0, juce::KeyPress::tabKey));
    CHECK (r.menu.getKeyboardItem() == 1);
    CHECK (r.key (1, juce::KeyPress::tabKey, shift));
    CHECK (r.menu.getKeyboardItem() == 0);
    CHECK (r.key (0, juce::KeyPress::leftKey));
    CHECK (r.menu.getKeyboardItem() == 3);
    CHECK (r.key (3, juce::KeyPress::rightKey));
    CHECK (r.menu.getKeyboardItem() == 0);
    r.said();

    CHECK (r.key (3, juce::KeyPress::returnKey));
    CHECK (r.said() == "action copy");
    CHECK_FALSE (r.key (0, juce::KeyPress::returnKey));
    CHECK (r.key (2, juce::KeyPress::escapeKey));
    CHECK (r.said() == "close");
    /* With nobody to close it, Escape is left to the owner. */
    r.menu.onClose = nullptr;
    CHECK_FALSE (r.key (2, juce::KeyPress::escapeKey));
    /* Space is the host's. */
    CHECK_FALSE (r.key (3, juce::KeyPress::spaceKey));
    CHECK_FALSE (r.key (0, juce::KeyPress::spaceKey, shift));
}

/* ------------------------------------------------------- hint and owner */

TEST_CASE ("radial menu: the hint names the hot item -- dragged, else pointed at, else focused")
{
    Recorded r;
    Pointer p;
    p.enter (r.menu.item (1), r.at (1));
    CHECK (r.said() == "hint glide");
    p.exit (r.menu.item (1));
    CHECK (r.said() == "hint ");

    r.menu.item (2).focusGained (juce::Component::focusChangedByTabKey);
    CHECK (r.said() == "hint level");
    p.enter (r.menu.item (0), r.at (0));
    CHECK (r.said() == "hint depth");
    p.exit (r.menu.item (0));
    CHECK (r.said() == "hint level");
    r.menu.item (2).focusLost (juce::Component::focusChangedDirectly);
    CHECK (r.said() == "hint ");

    /* Focus a click gave is not shown, so it is not hot. */
    r.menu.item (0).focusGained (juce::Component::focusChangedByMouseClick);
    CHECK (r.menu.getHot() == -1);
    CHECK (r.said() == "");
}

TEST_CASE ("radial menu: new values under a drag keep it; new ids end it")
{
    Recorded r;
    Pointer p;
    auto* first = &r.menu.item (0);
    p.down (r.menu.item (0), r.at (0));
    p.drag (r.at (0) - juce::Point<float> (0.0f, 20.0f));

    auto moved = r.items;
    moved[0].value01 = 0.6;
    moved[0].valueText = "60 %";
    r.menu.setItems (moved);
    CHECK (&r.menu.item (0) == first);
    CHECK (r.menu.getItems()[0].valueText == "60 %");

    /* Still measured from the press, from the value it had then. */
    p.drag (r.at (0) - juce::Point<float> (0.0f, 40.0f));
    p.up();
    CHECK (r.said() == "hint depth, begin depth, input depth 0.600, input depth 0.700, hint , end depth");
    CHECK (r.menu.getHot() == -1);

    auto other = r.items;
    other[0].id = "other";
    r.menu.setItems (other);
    CHECK (r.menu.getItems()[0].id == "other");
    CHECK (r.menu.getKeyboardItem() == -1);
}

TEST_CASE ("radial menu: a callback may delete it")
{
    auto window = std::make_unique<juce::Component>();
    window->setBounds (0, 0, 600, 400);
    auto menu = std::make_unique<RadialMenu>();
    window->addAndMakeVisible (*menu);
    menu->setItems ({ value ("a", 0.5), action ("b") });
    menu->placeAround ({ 300, 200 }, window->getLocalBounds());

    int calls = 0;
    menu->onAction = [&] (const juce::String&) { ++calls; menu.reset(); };
    menu->onClose = [&] { ++calls; menu.reset(); };
    menu->onBegin = [&] (const juce::String&, bool) { ++calls; menu.reset(); };

    Pointer p;
    auto& b = menu->item (1);
    const auto at = menu->itemCentre (1) - b.getPosition().toFloat();
    p.click (b, at);
    CHECK (menu == nullptr);

    menu = std::make_unique<RadialMenu>();
    menu->setItems ({ value ("a", 0.5), action ("b") });
    menu->onClose = [&] { ++calls; menu.reset(); };
    CHECK (menu->item (0).keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (menu == nullptr);

    menu = std::make_unique<RadialMenu>();
    menu->setItems ({ value ("a", 0.5), action ("b") });
    menu->onBegin = [&] (const juce::String&, bool) { ++calls; menu.reset(); };
    CHECK (menu->item (0).keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
    CHECK (menu == nullptr);
    CHECK (calls == 3);
}

/* -------------------------------------------------------- screen reader */

TEST_CASE ("radial menu: a popup menu of sliders and pressable items, each named by its label")
{
    Recorded r;
    auto menuHandler = r.menu.createAccessibilityHandler();
    CHECK (menuHandler->getRole() == juce::AccessibilityRole::popupMenu);
    CHECK (r.menu.getTitle() == "3 notes");

    auto valueHandler = r.menu.item (0).createAccessibilityHandler();
    CHECK (valueHandler->getRole() == juce::AccessibilityRole::slider);
    CHECK (r.menu.item (0).getTitle() == "depth");
    auto* v = valueHandler->getValueInterface();
    REQUIRE (v != nullptr);
    CHECK (v->getCurrentValue() == doctest::Approx (0.5));
    CHECK (v->getCurrentValueAsString() == "50 %");
    v->setValue (0.8);
    CHECK (r.said() == "begin depth, input depth 0.800, end depth");

    auto actionHandler = r.menu.item (3).createAccessibilityHandler();
    CHECK (actionHandler->getRole() == juce::AccessibilityRole::menuItem);
    CHECK (actionHandler->getActions().invoke (juce::AccessibilityActionType::press));
    CHECK (r.said() == "action copy");
}

/* -------------------------------------------------------------- picture */

NI_SNAPSHOT_TEST ("radial menu: at rest, pointed at, focused with one off, at a corner")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("controls-radial-menu")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "controls-radial-menu");
}

NI_SNAPSHOT_TEST ("radial menu: a value being dragged, lit in the middle")
{
    Recorded r;
    Pointer p;
    p.down (r.menu.item (2), r.at (2));
    p.drag (r.at (2) - juce::Point<float> (0.0f, 30.0f));
    auto items = r.items;
    items[2].value01 = 0.5;
    items[2].valueText = juce::String::fromUTF8 ("\xe2\x88\x92" "6.0 dB");
    r.menu.setItems (items);
    NI_CHECK_SNAPSHOT (r.menu, "controls-radial-menu-drag");
    p.up();
}

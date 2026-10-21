// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The RadialMenu: its ring round the point and kept inside the window, what
 * is its to hit and what is left to the owner (the middle too, when it
 * passes presses through), one Tab stop, a value item's drag as a
 * Knob's (the dead zone, the travel, Shift finer or the alternate, the
 * detents), a click on an action, a double-click's reset, every key, the
 * hint it names, a gesture kept across setItems, and what a screen reader
 * hears. See-through: the band no item and no label enters, the arcs and
 * the ring widened to hold them, only its items its own, the hot value in
 * its label, the keyboard as ever, and the plain ring untouched by it. Then
 * its gallery pages, as pictures.
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

TEST_CASE ("radial menu: the middle passes presses through when asked -- the ring, items and labels stay the menu's")
{
    Recorded r;
    r.window.setVisible (true);   // getComponentAt finds nothing in a hidden one
    /* What is under the menu: the owner's object. */
    juce::Component under;
    r.window.addAndMakeVisible (under, 0);
    under.setBounds (r.window.getLocalBounds());
    const auto centre = r.menu.getCentrePoint();
    const auto inMenu = [&r] (juce::Point<int> p) { return p - r.menu.getPosition(); };
    const auto at = [&centre] (float radius, float degrees)
    {
        const float a = juce::degreesToRadians (degrees);
        return (centre.toFloat() + juce::Point<float> (std::sin (a), -std::cos (a)) * radius).roundToInt();
    };

    CHECK (RadialMenu::holeRadius == doctest::Approx (36.0f));
    CHECK_FALSE (r.menu.passesCentreThrough());
    CHECK (r.window.getComponentAt (centre) == &r.menu);

    r.menu.setCentrePassesThrough (true);
    CHECK (r.menu.passesCentreThrough());
    /* The hole: the object under it gets the press, and so its drag and
     * its double-click. */
    for (const auto p : { centre, at (20.0f, 45.0f), at (RadialMenu::holeRadius - 1.0f, 200.0f) })
    {
        CAPTURE (p.toString());
        CHECK_FALSE (r.menu.hitTest (inMenu (p).x, inMenu (p).y));
        CHECK (r.window.getComponentAt (p) == &under);
    }
    /* The ring between the items, the plate past it: the menu's, doing nothing. */
    for (const auto p : { at (RadialMenu::holeRadius + 2.0f, 45.0f), at (70.0f, 45.0f), at (80.0f, 135.0f) })
    {
        CAPTURE (p.toString());
        CHECK (r.menu.hitTest (inMenu (p).x, inMenu (p).y));
        CHECK (r.window.getComponentAt (p) == &r.menu);
    }
    /* Every item and its label: the item's. */
    for (int i = 0; i < 4; ++i)
    {
        CAPTURE (i);
        const auto c = (r.menu.itemCentre (i) + r.menu.getPosition().toFloat()).roundToInt();
        CHECK (r.window.getComponentAt (c) == &r.menu.item (i));
        const auto chip = (r.menu.labelBounds (i).getCentre() + r.menu.getPosition().toFloat()).roundToInt();
        CHECK (r.window.getComponentAt (chip) == &r.menu.item (i));
    }
    /* Outside the plate, as ever: the owner's. */
    CHECK (r.window.getComponentAt (at (RadialMenu::plateRadius + 12.0f, 45.0f)) == &under);

    r.menu.setCentrePassesThrough (false);
    CHECK (r.window.getComponentAt (centre) == &r.menu);
}

TEST_CASE ("radial menu: one Tab stop -- Tab into it takes the first live item, or the last one used; Shift+Tab leaves it")
{
    juce::Component window;
    window.setBounds (0, 0, 800, 600);
    window.setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    juce::TextButton before ("before"), after ("after");
    RadialMenu menu;
    before.setExplicitFocusOrder (1);
    menu.setExplicitFocusOrder (2);
    after.setExplicitFocusOrder (3);
    auto items = std::vector<Item> { value ("depth", 0.5), value ("glide", 0.4), action ("copy") };
    items[0].enabled = false;
    menu.setItems (items);
    for (auto* c : std::initializer_list<juce::Component*> { &before, &menu, &after })
        window.addAndMakeVisible (*c);
    before.setBounds (10, 10, 80, 28);
    after.setBounds (700, 10, 80, 28);
    menu.placeAround ({ 400, 300 }, window.getLocalBounds());

    CHECK (menu.getWantsKeyboardFocus());
    CHECK (menu.isKeyboardFocusContainer());
    CHECK_FALSE (menu.getMouseClickGrabsKeyboardFocus());
    juce::KeyboardFocusTraverser traverser;
    CHECK (traverser.getAllComponents (&window) == std::vector<juce::Component*> { &before, &menu, &after });
    CHECK (traverser.getNextComponent (&before) == &menu);
    CHECK (traverser.getPreviousComponent (&menu) == &before);
    CHECK (traverser.getNextComponent (&menu) == &after);

    /* In: the first live item, past the one that is off. */
    menu.focusGained (juce::Component::focusChangedByTabKey);
    CHECK (menu.getKeyboardItem() == 1);
    /* The keyboard on the last item; in again later, it goes back there. */
    menu.item (2).focusGained (juce::Component::focusChangedByTabKey);
    CHECK (menu.getKeyboardItem() == 2);
    menu.focusGained (juce::Component::focusChangedByTabKey);
    CHECK (menu.getKeyboardItem() == 2);
    /* Tab goes round, past the one that is off. */
    CHECK (menu.item (2).keyPressed (juce::KeyPress (juce::KeyPress::tabKey)));
    CHECK (menu.getKeyboardItem() == 1);
    /* Shift+Tab is used, and moves no item: it leaves the menu. */
    CHECK (menu.item (1).keyPressed (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0)));
    CHECK (menu.getKeyboardItem() == 1);
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
    CHECK (r.key (3, juce::KeyPress::tabKey));
    CHECK (r.menu.getKeyboardItem() == 0);
    /* Shift+Tab leaves the menu (the Tab stop's test): no item moves. */
    CHECK (r.key (0, juce::KeyPress::tabKey, shift));
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

/* ---------------------------------------------------------- see-through */

namespace
{
/* n items, values and actions mixed, each with a label. */
std::vector<Item> mixed (int n)
{
    const char* ids[] = { "depth", "rate", "copy", "level", "tone", "link", "mix", "pan" };
    std::vector<Item> out;
    for (int i = 0; i < n; ++i)
        out.push_back (i % 3 == 2 ? action (ids[i]) : value (ids[i], 0.1 * (i + 1)));
    return out;
}

/* A see-through menu of n items over an object that fills the window. */
struct SeeThrough
{
    SeeThrough (int n, float band = RadialMenu::defaultClearBand)
    {
        window.setBounds (0, 0, 800, 600);
        window.addAndMakeVisible (under);
        under.setBounds (window.getLocalBounds());
        window.addAndMakeVisible (menu);
        menu.setSeeThrough (true);
        menu.setClearBand (band);
        menu.setItems (mixed (n));
        menu.placeAround ({ 400, 300 }, window.getLocalBounds());
    }

    /* The band, as wide as the window, in the menu's coordinates. */
    juce::Rectangle<float> band() const
    {
        const auto c = (menu.getCentrePoint() - menu.getPosition()).toFloat();
        return { -1000.0f, c.y - menu.getClearBand(), 3000.0f, 2.0f * menu.getClearBand() };
    }

    juce::Point<int> inWindow (juce::Point<float> inMenu) const { return (inMenu + menu.getPosition().toFloat()).roundToInt(); }
    juce::Point<float> at (int i) { return menu.itemCentre (i) - menu.item (i).getPosition().toFloat(); }

    juce::Component window, under;
    RadialMenu menu;
};
} // namespace

TEST_CASE ("radial menu, see-through: no dial and no label enters the band -- 3 to 8 items, 10 and 16px")
{
    for (const float half : { RadialMenu::defaultClearBand, 16.0f })
        for (int n = 3; n <= 8; ++n)
        {
            CAPTURE (half);
            CAPTURE (n);
            SeeThrough s (n, half);
            const auto& m = s.menu;
            REQUIRE (m.isSeeThrough());
            CHECK (m.getClearBand() == half);
            CHECK (m.getCentrePoint() == juce::Point<int> (400, 300));
            const auto centre = (m.getCentrePoint() - m.getPosition()).toFloat();
            const auto band = s.band();
            const int upper = (n + 1) / 2;

            /* The ring: 56px while its arcs hold the items -- three a side
             * round a 10px band, two round 16px -- and wider past that. */
            if (n <= (half <= RadialMenu::defaultClearBand ? 6 : 4))
                CHECK (m.getRingRadius() == RadialMenu::ringRadius);
            else
                CHECK (m.getRingRadius() > RadialMenu::ringRadius);
            CHECK (m.getRingRadius() < 80.0f);

            Pointer p;
            for (int i = 0; i < n; ++i)
            {
                CAPTURE (i);
                const auto c = m.itemCentre (i);
                CHECK (c.getDistanceFrom (centre) == doctest::Approx (m.getRingRadius()));
                /* The whole dial, ticks and all, 2px clear of the band. */
                CHECK (std::abs (c.y - centre.y) >= half + RadialMenu::itemReach + RadialMenu::bandGap - 1.0e-3f);
                CHECK_FALSE (juce::Rectangle<float> (RadialMenu::itemBox, RadialMenu::itemBox).withCentre (c).intersects (band));
                /* Its label, and the label with its value while hot. */
                CHECK_FALSE (m.labelBounds (i).intersects (band));
                p.enter (s.menu.item (i), s.at (i));
                CHECK (m.getHot() == i);
                CHECK_FALSE (m.labelBounds (i).intersects (band));
                CHECK (m.getLocalBounds().toFloat().contains (m.labelBounds (i)));
                p.exit (s.menu.item (i));

                /* The first half above, the rest below; outward labels. */
                CHECK ((i < upper) == (c.y < centre.y));
                CHECK ((i < upper) == (m.labelBounds (i).getBottom() < c.y));
                CHECK ((i < upper) != (m.labelBounds (i).getY() > c.y));

                for (int j = i + 1; j < n; ++j)
                {
                    CAPTURE (j);
                    /* Rails 2px apart, and no label on another's label or dial. */
                    CHECK (c.getDistanceFrom (m.itemCentre (j)) >= 2.0f * RadialMenu::railRadius + RadialMenu::railGap - 1.0e-3f);
                    CHECK_FALSE (m.labelBounds (i).intersects (m.labelBounds (j)));
                    CHECK_FALSE (m.labelBounds (i).intersects (juce::Rectangle<float> (RadialMenu::discSize, RadialMenu::discSize).withCentre (m.itemCentre (j))));
                }
            }

            /* Clockwise from the top left: the upper arc left to right, the
             * lower right to left, so the keyboard goes round as it reads. */
            for (int i = 1; i < n; ++i)
            {
                CAPTURE (i);
                if (i < upper)
                    CHECK (m.itemCentre (i).x > m.itemCentre (i - 1).x);
                else if (i > upper)
                    CHECK (m.itemCentre (i).x < m.itemCentre (i - 1).x);
            }
            if (n - upper == 1)
                CHECK (m.itemCentre (upper).x == doctest::Approx (centre.x));
            CHECK (m.itemCentre (0).x < centre.x);
        }
}

TEST_CASE ("radial menu, see-through: the widened ring is the smallest that holds the arcs")
{
    /* Seven at 10px: four above, rails 2px apart, each 26px clear of the
     * centre line. */
    SeeThrough s (7);
    const float r = s.menu.getRingRadius();
    const float rise = RadialMenu::defaultClearBand + RadialMenu::itemReach + RadialMenu::bandGap;
    const float apart = RadialMenu::railRadius + RadialMenu::railGap * 0.5f;
    const auto holds = [&] (float radius) { return 2.0f * std::acos (rise / radius) >= 3.0f * 2.0f * std::asin (apart / radius); };
    CHECK (holds (r));
    CHECK_FALSE (holds (r - 1.0f));
    CHECK (r == std::ceil (r));

    /* A band too high for the plain ring even for one item a side. */
    SeeThrough tall (2, 40.0f);
    CHECK (tall.menu.getRingRadius() >= 40.0f + rise - RadialMenu::defaultClearBand);
    CHECK_FALSE (tall.menu.labelBounds (0).intersects (tall.band()));
    CHECK (std::abs (tall.menu.itemCentre (1).y - tall.menu.itemCentre (0).y) >= 2.0f * (40.0f + RadialMenu::itemReach));

    /* A smaller band never makes it smaller than the plain ring. */
    SeeThrough flat (4, 0.0f);
    CHECK (flat.menu.getRingRadius() == RadialMenu::ringRadius);
    flat.menu.setClearBand (-5.0f);
    CHECK (flat.menu.getClearBand() == 0.0f);
}

TEST_CASE ("radial menu, see-through: only its dials and labels are its own -- the centre, the band and between pass through")
{
    for (const int n : { 4, 7 })
    {
        CAPTURE (n);
        SeeThrough s (n);
        s.window.setVisible (true);   // getComponentAt finds nothing in a hidden one
        auto& m = s.menu;
        const auto centre = m.getCentrePoint();
        const auto local = [&m] (juce::Point<int> p) { return p - m.getPosition(); };

        /* The centre and the band, either way along it: the object's. */
        for (const int dx : { 0, -30, 30, -60, 60, -90, 90 })
            for (const int dy : { 0, -9, 9 })
            {
                const auto p = centre + juce::Point<int> (dx, dy);
                CAPTURE (p.toString());
                CHECK_FALSE (m.hitTest (local (p).x, local (p).y));
                CHECK (s.window.getComponentAt (p) == &s.under);
            }
        /* Between neighbours, where the plate was: the object's. */
        for (int i = 0; i + 1 < n; ++i)
        {
            const auto mid = s.inWindow ((m.itemCentre (i) + m.itemCentre (i + 1)) * 0.5f);
            CAPTURE (mid.toString());
            CHECK (s.window.getComponentAt (mid) == &s.under);
        }
        /* Passing the centre through or not changes nothing here. */
        m.setCentrePassesThrough (false);
        CHECK (s.window.getComponentAt (centre) == &s.under);
        m.setCentrePassesThrough (true);
        CHECK (s.window.getComponentAt (centre) == &s.under);

        /* Every dial, to its reach, and every label: the item's. */
        for (int i = 0; i < n; ++i)
        {
            CAPTURE (i);
            const auto c = m.itemCentre (i);
            CHECK (s.window.getComponentAt (s.inWindow (c)) == &m.item (i));
            CHECK (s.window.getComponentAt (s.inWindow (c + juce::Point<float> (RadialMenu::railRadius - 1.0f, 0.0f))) == &m.item (i));
            CHECK (s.window.getComponentAt (s.inWindow (m.labelBounds (i).getCentre())) == &m.item (i));
        }
    }
}

TEST_CASE ("radial menu, see-through: the hot value's label carries its value; an action's only its label")
{
    SeeThrough s (4);
    auto items = mixed (4);
    items[0].valueText = juce::String::fromUTF8 ("\xe2\x88\x92" "3.0 dB");
    s.menu.setItems (items);
    auto& m = s.menu;
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");

    CHECK (m.labelText (0) == "DEPTH");
    const auto resting = m.labelBounds (0);
    Pointer p;
    p.enter (m.item (0), s.at (0));
    CHECK (m.labelText (0) == "DEPTH" + dot + juce::String::fromUTF8 ("\xe2\x88\x92" "3.0 dB"));
    const auto hot = m.labelBounds (0);
    CHECK (hot.getWidth() > resting.getWidth() + 30.0f);
    /* Upper left: it grows away from the centre, its inner corner kept. */
    CHECK (hot.getRight() == doctest::Approx (resting.getRight()));
    CHECK (hot.getBottom() == doctest::Approx (resting.getBottom()));
    /* Its wider part is its own, before any neighbour's. */
    CHECK (m.itemAt (hot.getTopLeft() + juce::Point<float> (2.0f, 2.0f)) == 0);
    p.exit (m.item (0));
    CHECK (m.labelText (0) == "DEPTH");
    CHECK (m.labelBounds (0) == resting);

    /* An action pointed at: its label alone. */
    p.enter (m.item (2), s.at (2));
    CHECK (m.labelText (2) == "COPY");
    p.exit (m.item (2));

    /* Dragged, the value it is set to; focused by the keyboard, too. */
    p.down (m.item (1), s.at (1));
    p.drag (s.at (1) - juce::Point<float> (0.0f, 20.0f));
    items[1].valueText = "30 %";
    m.setItems (items);
    CHECK (m.labelText (1) == "RATE" + dot + "30 %");
    p.up();
    CHECK (m.labelText (1) == "RATE");
    m.item (3).focusGained (juce::Component::focusChangedByTabKey);
    CHECK (m.labelText (3) == "LEVEL" + dot + "40 %");

    /* The plain ring keeps its values in the middle. */
    m.setSeeThrough (false);
    CHECK (m.labelText (3) == "LEVEL");
}

TEST_CASE ("radial menu, see-through: the keys go round as before, clockwise as it reads")
{
    SeeThrough s (5);
    auto& m = s.menu;
    juce::StringArray log;
    m.onBegin = [&] (const juce::String& id, bool) { log.add ("begin " + id); };
    m.onInput = [&] (const juce::String& id, double v, bool) { log.add ("input " + id + " " + juce::String (v, 3)); };
    m.onEnd = [&] (const juce::String& id) { log.add ("end " + id); };
    m.onClose = [&] { log.add ("close"); };
    const auto key = [&m] (int i, int code) { return m.item (i).keyPressed (juce::KeyPress (code, {}, 0)); };

    /* The items' order is the clock's: each one further round from 12. */
    const auto centre = (m.getCentrePoint() - m.getPosition()).toFloat();
    float last = -juce::MathConstants<float>::pi;
    for (int i = 0; i < 5; ++i)
    {
        CAPTURE (i);
        const auto d = m.itemCentre (i) - centre;
        float a = std::atan2 (d.x, -d.y);   // clockwise from 12 o'clock
        if (i > 0 && a < 0.0f)
            a += juce::MathConstants<float>::twoPi;
        CHECK (a > last);
        last = a;
    }

    CHECK (key (0, juce::KeyPress::rightKey));
    CHECK (m.getKeyboardItem() == 1);
    CHECK (key (1, juce::KeyPress::tabKey));
    CHECK (m.getKeyboardItem() == 2);
    CHECK (key (0, juce::KeyPress::leftKey));
    CHECK (m.getKeyboardItem() == 4);
    CHECK (key (4, juce::KeyPress::rightKey));
    CHECK (m.getKeyboardItem() == 0);
    CHECK (key (0, juce::KeyPress::upKey));
    CHECK (key (0, juce::KeyPress::escapeKey));
    CHECK (log.joinIntoString (", ") == "begin depth, input depth 0.110, end depth, close");
}

TEST_CASE ("radial menu, see-through: placed as the plain one is, its footprint the items and labels only")
{
    SeeThrough s (4);
    auto& m = s.menu;
    const auto window = s.window.getLocalBounds();
    /* No plate: every dial and every label. */
    juce::Rectangle<float> all;
    for (int i = 0; i < 4; ++i)
        all = all.getUnion (juce::Rectangle<float> (RadialMenu::itemBox, RadialMenu::itemBox).withCentre (m.itemCentre (i)))
                  .getUnion (m.labelBounds (i));
    CHECK ((m.footprint() - m.getPosition()) == all.getSmallestIntegerContainer());
    /* The menu reaches past it for its light, and for a chip with its value. */
    CHECK (m.getBounds().contains (m.footprint().expanded (RadialMenu::lightMargin)));

    Pointer p;
    for (const auto at : { juce::Point<int> (0, 0), juce::Point<int> (800, 600), juce::Point<int> (400, 2),
                           juce::Point<int> (3, 300), juce::Point<int> (797, 300) })
    {
        CAPTURE (at.toString());
        m.placeAround (at, window);
        CHECK (window.contains (m.footprint()));
        /* The band moved with the menu: still its centre line. */
        const auto c = (m.getCentrePoint() - m.getPosition()).toFloat();
        CHECK (s.band().getCentreY() == doctest::Approx (c.y));
        const auto placed = m.getCentrePoint();
        for (int i = 0; i < 4; ++i)
        {
            CAPTURE (i);
            CHECK_FALSE (m.labelBounds (i).intersects (s.band()));
            /* Pointed at, its value's chip stays inside too, and nothing moves. */
            p.enter (m.item (i), s.at (i));
            const auto chip = m.labelBounds (i) + m.getPosition().toFloat();
            CHECK (window.toFloat().contains (chip));
            CHECK (m.getBounds().toFloat().contains (chip));
            CHECK (m.itemAt (m.labelBounds (i).getCentre()) == i);
            CHECK (m.getCentrePoint() == placed);
            p.exit (m.item (i));
        }
    }
}

TEST_CASE ("radial menu, see-through: a value's chip by 12 o'clock grows away from it, over no item across")
{
    SeeThrough s (8);
    auto& m = s.menu;
    Pointer p;
    /* Above: 0 and 1 left of 12, 2 and 3 right of it. */
    for (const int i : { 1, 2 })
    {
        CAPTURE (i);
        const auto resting = m.labelBounds (i);
        p.enter (m.item (i), s.at (i));
        const auto hot = m.labelBounds (i);
        if (i == 1)
            CHECK (hot.getRight() == doctest::Approx (resting.getRight()));
        else
            CHECK (hot.getX() == doctest::Approx (resting.getX()));
        CHECK_FALSE (hot.intersects (m.labelBounds (i == 1 ? 2 : 1)));
        p.exit (m.item (i));
    }
}

TEST_CASE ("radial menu, see-through: off again, the plain ring is exactly as it was")
{
    Recorded r;
    std::vector<juce::Point<float>> centres;
    std::vector<juce::Rectangle<float>> labels;
    for (int i = 0; i < 4; ++i)
    {
        centres.push_back (r.menu.itemCentre (i));
        labels.push_back (r.menu.labelBounds (i));
    }
    const auto bounds = r.menu.getBounds();

    r.menu.setSeeThrough (true);
    CHECK (r.menu.getBounds() != bounds);
    CHECK (r.menu.itemCentre (1) != centres[1]);
    r.menu.setSeeThrough (false);
    CHECK (r.menu.getBounds() == bounds);
    CHECK (r.menu.getRingRadius() == RadialMenu::ringRadius);
    for (int i = 0; i < 4; ++i)
    {
        CAPTURE (i);
        CHECK (r.menu.itemCentre (i) == centres[(size_t) i]);
        CHECK (r.menu.labelBounds (i) == labels[(size_t) i]);
    }
    const auto c = r.menu.getCentrePoint() - r.menu.getPosition();
    CHECK (r.menu.hitTest (c.x, c.y));
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

NI_SNAPSHOT_TEST ("radial menu, see-through: over an object, a value pointed at, seven round a 16px band, at a corner")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("controls-radial-menu-see-through")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "controls-radial-menu-see-through");
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

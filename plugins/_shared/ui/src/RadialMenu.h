// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A circle of controls around a point: what a click on an object in a well
 * opens, so the object's values and verbs are where the object is and no tool
 * has to be picked first.
 *
 * THE DRAWING, around the point it was placed at:
 *
 *   plate      a bg-100 disc of plateRadius (84px) on a line-200 hairline,
 *              which hides the well under the ring so its values read; its
 *              edge stays quiet, so the lit arcs are the brightest thing in it
 *   items      3 to 8, evenly round a 56px ring, the first at 12 o'clock and
 *              the rest clockwise; each a 32px bg-200 disc on a line-100
 *              hairline, bg-300 while hot (pointed at, dragged, or holding
 *              visible keyboard focus)
 *   a value    is a knob's dial at the knob's own radii (Knob.h): the
 *              270-degree line-200 rail at 20px, the uv value arc with its
 *              arc glow from the minimum (from 12 o'clock when bipolar),
 *              detent ticks from 22 to 24px, and the 2px pointer -- or, when
 *              the item has a glyph, the glyph in the disc instead of the
 *              pointer
 *   an action  the disc and its glyph, no rail
 *   labels     each item's label outside the plate, along the item's
 *              direction, in the label style on a bg-100 chip so it reads
 *              over anything: ink-muted, ink while hot, ink-dim disabled
 *   centre     the owner's words ("3 notes") in the value style, ink-muted;
 *              while a value item is hot, its value text instead, as a
 *              readout prints one (number in ink, unit in ink-muted)
 *   focus      glow-focus round the item that has visible keyboard focus
 *
 * Glyphs are the system's or the product's own (UvIcons.h), never invented;
 * an item without one is its label alone.
 *
 * THE POINTER, on an item, as a Knob (Interaction conventions):
 *
 *   drag          a value item: vertical, 200px for the range, Shift five
 *                 times finer, detents held for 14px (Detents.h), measured
 *                 from the press. Nothing happens inside the first 4px either
 *                 way (deadZone); the gesture opens on the first move past
 *                 them -- onBegin, then onInput for every move, onEnd on the
 *                 release -- so a click is never an edit
 *   Shift         chosen at the press. On an item with shiftIsAlternate it is
 *                 not fine control but the item's other parameter: the drag
 *                 runs from alternate01, ignores the detents, and reports
 *                 alternate = true -- say so in the item's hint line
 *   click         an action item: onAction on the release, if the pointer
 *                 stayed inside the dead zone
 *   double-click  a value item: onReset, the owner's default
 *
 * A press on the plate or a label is the menu's and does nothing. A press
 * anywhere else is NOT the menu's: hitTest() leaves everything outside the
 * plate and the labels to whatever is under it, so the owner, which places
 * the menu in its overlay and hears that press, closes it and still does
 * what the press does there -- in one click. (Inside a Popup the press
 * would be swallowed; host it there only if that is what you want.)
 *
 * THE MIDDLE MAY PASS THROUGH (setCentrePassesThrough): the menu opens
 * round the object that was clicked, and its plate covers it. An owner that
 * wants that object still in reach turns this on: a press inside the hole
 * -- nearer the centre than the items' inner edge, holeRadius -- is then
 * not the menu's either, and goes to whatever is under it, its drags and
 * double-clicks with it. The ring between the items, the items and their
 * labels stay the menu's. The middle is still drawn, words and all; off by
 * default.
 *
 * SEE-THROUGH (setSeeThrough): for an owner whose object must stay in sight
 * while its controls are open -- a note under the click, its whole row. The
 * menu then draws nothing between its items and claims nothing there:
 *
 *   no plate   no fill, no hairline, no centre text; whatever is under the
 *              menu shows between the items, untouched
 *   the band   a clear horizontal band through the centre, clearBand (10px)
 *              above and below it (setClearBand), which no item's dial and
 *              no label enters
 *   the arcs   the items on two arcs, one above the band round 12 o'clock
 *              and one below it round 6: the first half (one more if odd)
 *              above, the rest below, each arc evenly spread over the angles
 *              whose items clear the band by 2px -- a dial's 24px reach,
 *              its ticks, measured from the band's edge. Clockwise from the
 *              top left: the upper arc left to right, then the lower right
 *              to left, so Tab and the arrows go round as they read
 *   wider      where an arc cannot hold its items at 56px with their rails
 *              2px apart -- seven or eight items, or a band over 10px --
 *              the ring widens just enough that it can (getRingRadius)
 *   labels     outside each dial along its direction, as ever, but always
 *              on the side away from the band: above the upper arc's items,
 *              below the lower's
 *   a value    with no centre to print it, the hot value item's chip says
 *              "LABEL · value": the label in ink, the value as a readout
 *              prints one (number in ink, unit in ink-muted); painted over
 *              its neighbours
 *
 * What is the menu's is only its items: their dials (railRadius, as ever)
 * and their chips. Every other point -- the centre, the band, the space
 * between the items -- is NOT the menu's, so the object under it is
 * pressed, dragged and double-clicked as if the menu were not there.
 * setCentrePassesThrough then changes nothing (see-through takes
 * precedence; it is kept for when see-through is turned off again). The
 * centre text is not drawn, but still titles the menu for a screen reader:
 * an owner shows it elsewhere -- its hint bar, say.
 *
 * placeAround keeps the items and their labels inside the bounds; the band
 * is the menu's centre line, so it lies on the point asked for unless the
 * menu had to move to fit, and then it moves with the menu (an owner that
 * minds places it again, or keeps its objects off the edge).
 *
 * THE KEYBOARD. The menu is one Tab stop: Tab from the control before it
 * gives it the keyboard, and its first enabled item -- or the one the
 * keyboard was last on -- takes it. On the item that has it:
 *
 *   Tab           the next item, clockwise, round and round
 *   Shift+Tab     out of the menu, to the stop before it
 *
 *   Left / Right  the item before or after, counter-clockwise or clockwise
 *   Up / Down     a value item: 1 % (Shift 0.2 %, or the alternate at 1 %);
 *                 Page Up / Down the next detent or 10 %; Home / End the
 *                 ends -- each key one complete edit: onBegin, onInput, onEnd
 *   Enter         an action item: onAction
 *   Escape        onClose
 *
 * The owner may also give it the keyboard with grabKeyboardFocus(). A press
 * on the menu -- an item or the plate -- does not take the keyboard, so the
 * owner's own keys -- undo, its Escape -- keep working while it is open.
 *
 * IT CHANGES NOTHING ITSELF, and never closes itself. Every value it shows is
 * the owner's (setItems, which keeps a gesture under way when the ids are
 * the same); onInput reports what the drag asks for, and the owner sets it
 * back. onHint names the hot item -- the pointer's, else the keyboard's --
 * or "" for none, for the owner's hint bar; onClose asks to be closed.
 * Every callback may delete the menu: it is the last thing a handler does.
 *
 * To a screen reader the menu is a popup menu titled by its centre text, a
 * value item a slider (its label the name, its value text the value), an
 * action a menu item that can be pressed.
 *
 * THE RULE is Ultraviolet 2.4.0's Interaction conventions, "Radial menu"
 * (design/scheme/project/README.md; the component's guidelines are
 * design/scheme/project/components/RadialMenu/README.md).
 */
#pragma once

#include "Focus.h"
#include "Knob.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace ni::ui
{

class RadialMenu : public juce::Component
{
public:
    /* The ring: the items' centres this far from the point, their discs,
     * and the dial round a value item (Knob.h's radii). */
    static constexpr float ringRadius = 56.0f;
    static constexpr float discSize = 32.0f;
    static constexpr float railRadius = 20.0f;
    static constexpr float itemBox = (float) Knob::dialSize;
    /* The plate under the ring, and the labels' distance outside it. */
    static constexpr float plateRadius = ringRadius + itemBox * 0.5f + 4.0f;
    static constexpr float labelGap = 4.0f;
    /* Past the plate and the labels, the room the focus glow takes. */
    static constexpr int lightMargin = 16;

    /* The feel: a knob's, with a click's dead zone. */
    static constexpr double travelPx = Knob::travelPx;
    static constexpr double fineRatio = Knob::fineRatio;
    static constexpr float deadZone = 4.0f;

    /* What the ring holds comfortably: more crowd it. */
    static constexpr int maxItems = 8;
    /* See-through: the band's half-height by default; how far a dial
     * reaches from its centre (its detent ticks), and how clear of the band
     * it stays; and its rails' distance from a neighbour's, at least. */
    static constexpr float defaultClearBand = 10.0f;
    static constexpr float itemReach = itemBox * 0.5f;
    static constexpr float bandGap = 2.0f;
    static constexpr float railGap = 2.0f;
    /* The middle that may pass presses through: inside the items' inner
     * edge, 4px clear of it. */
    static constexpr float holeRadius = ringRadius - discSize * 0.5f - 4.0f;

    struct Item
    {
        enum class Kind { value, action };

        juce::String id;
        /* A glyph (uv::hasIcon), or empty for the label alone. */
        juce::String icon;
        juce::String label;
        Kind kind = Kind::value;
        /* A value item: its value, normalised; whether its arc starts at 12
         * o'clock; the values a drag holds on; its text ("40 %"). */
        double value01 = 0.0;
        bool bipolar = false;
        std::vector<double> detents;
        juce::String valueText;
        bool enabled = true;
        /* Shift is the item's other parameter rather than fine control, and
         * a Shift drag starts from alternate01, that parameter's value. */
        bool shiftIsAlternate = false;
        double alternate01 = 0.0;
    };

    RadialMenu();
    ~RadialMenu() override;

    /* The items, clockwise from 12 o'clock. The same ids in the same order
     * keep the gesture, the pointer and the keyboard where they are. */
    void setItems (std::vector<Item>);
    const std::vector<Item>& getItems() const noexcept { return items; }

    /* The words in the middle while no value item is hot. */
    void setCentreText (const juce::String&);
    const juce::String& getCentreText() const noexcept { return centreText; }

    /*
     * Puts the ring's centre at `centre` -- or as near it as keeps the plate
     * and the labels inside `bounds` -- and sizes the menu round them. Both
     * in the parent's coordinates; the menu's own bounds reach lightMargin
     * further, for its light. Kept, and done again by setItems.
     */
    void placeAround (juce::Point<int> centre, juce::Rectangle<int> bounds);
    /* Presses inside holeRadius go to what is under the menu (false: the
     * menu's, as the rest of the plate). */
    void setCentrePassesThrough (bool);
    bool passesCentreThrough() const noexcept { return centrePasses; }

    /* See-through (above): no plate, no centre, a clear band through the
     * centre, and only the items and labels the menu's. Off by default. */
    void setSeeThrough (bool);
    bool isSeeThrough() const noexcept { return seeThrough; }
    /* The band's half-height, from the centre line to its edge (>= 0). */
    void setClearBand (float halfHeight);
    float getClearBand() const noexcept { return clearBand; }
    /* The ring's radius now: ringRadius, or wider where see-through's arcs
     * need it. */
    float getRingRadius() const noexcept { return ring; }

    /* Where the ring's centre ended up, in the parent's coordinates. */
    juce::Point<int> getCentrePoint() const noexcept { return placedAt; }
    /* The plate and the labels -- see-through, the items' dials and their
     * labels -- in the parent's coordinates. */
    juce::Rectangle<int> footprint() const;

    /* An item's centre and its label's chip, in the menu's coordinates; the
     * item under a point (its rail's circle or its label), or -1. */
    juce::Point<float> itemCentre (int index) const;
    juce::Rectangle<float> labelBounds (int index) const;
    int itemAt (juce::Point<float>) const;
    /* What an item's chip says now: its label as the label style sets it,
     * or, see-through while it is a hot value item, "LABEL · value". */
    juce::String labelText (int index) const;

    /* The component an item is: the Tab stop, the screen reader's element,
     * and where the pointer's events for it go. */
    juce::Component& item (int index);

    /* The hot item -- dragged, else pointed at, else holding visible
     * keyboard focus -- or -1; and the item the keyboard is on, or -1. */
    int getHot() const noexcept;
    int getKeyboardItem() const noexcept { return keyboardItem; }

    /* ---- what it asks for */
    std::function<void (const juce::String& id, bool alternate)> onBegin;
    std::function<void (const juce::String& id, double value01, bool alternate)> onInput;
    std::function<void (const juce::String& id)> onEnd;
    std::function<void (const juce::String& id)> onReset;
    std::function<void (const juce::String& id)> onAction;
    std::function<void (const juce::String& id)> onHint;
    std::function<void()> onClose;

    bool hitTest (int x, int y) override;
    void focusGained (FocusChangeType) override;
    void paint (juce::Graphics&) override;
    void enablementChanged() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class Control;

    void arrange();
    void layout();
    juce::Rectangle<int> footprintAboutCentre() const;
    juce::Point<float> ringCentre() const;
    float itemAngle (int index) const;
    juce::Point<float> direction (int index) const;
    bool showsValue (int index, bool hot) const;
    juce::Rectangle<float> chipAt (int index, juce::Point<float> centre, bool hot) const;
    void paintChip (juce::Graphics&, int index, bool hot) const;
    bool isLive (int index) const;
    void hotChanged();
    void move (int from, int by);
    void paintItem (juce::Graphics&, int index, bool hot) const;

    std::vector<Item> items;
    std::vector<std::unique_ptr<Control>> controls;
    juce::String centreText;

    juce::Point<int> requestedAt, placedAt;
    juce::Rectangle<int> within;
    bool placed = false;
    bool centrePasses = false;
    bool seeThrough = false;
    float clearBand = defaultClearBand;
    /* The ring's radius, and see-through's arcs' half-width, in radians
     * either side of 12 and of 6 o'clock (arrange). */
    float ring = ringRadius;
    float arcHalf = 0.0f;

    int hovered = -1, dragged = -1, keyboardItem = -1;
    int lastHot = -1;

    JUCE_DECLARE_NON_COPYABLE (RadialMenu)
};

} // namespace ni::ui

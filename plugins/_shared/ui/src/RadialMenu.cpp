// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The circle of controls. RadialMenu.h has its drawing, its feel and its
 * keys.
 *
 * The menu paints everything -- the plate, every item, the labels, the
 * centre, the focus -- in its own paint(), from its own state; an item's
 * Control paints nothing. A Control is what JUCE needs an item to be: a
 * component the pointer's events go to (its hit area the rail's circle and
 * the label), a Tab stop, and an element a screen reader can name.
 */
#include "RadialMenu.h"

#include "Detents.h"
#include "Keys.h"
#include "UvIcons.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

constexpr float hair = uv::tok::stroke::strokeHair;

/* The dial's radii inside the 48px item box: Knob.cpp's. */
constexpr float pointerFrom = 6.0f;
constexpr float pointerTo = 14.0f;
constexpr float tickFrom = 22.0f;
constexpr float tickTo = 24.0f;
constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
constexpr float sweep = juce::MathConstants<float>::pi * 1.5f;
constexpr double onDetent = 1.0e-4;

/* A label's chip: the label style's line box, 2px above and below, 6px
 * either side. */
constexpr float chipPadX = 6.0f;
constexpr float chipPadY = 2.0f;

/* The centre's line: as wide as the space between the rails at 3 and 9. */
constexpr float centreWidth = 2.0f * (RadialMenu::ringRadius - RadialMenu::railRadius);

/* How far a direction must lean to put the chip to that side of its anchor
 * rather than centred on it: sin 22.5 degrees, so the eight places of an
 * eight-item ring split cleanly. */
constexpr float lean = 0.38f;

float angleOf (double v)
{
    return startAngle + sweep * (float) juce::jlimit (0.0, 1.0, v);
}

juce::Path arcStroke (juce::Point<float> centre, float from, float to)
{
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, RadialMenu::railRadius, RadialMenu::railRadius, 0.0f, from, to, true);
    juce::Path stroke;
    juce::PathStrokeType (uv::tok::stroke::strokeRail, juce::PathStrokeType::mitered, juce::PathStrokeType::butt)
        .createStrokedPath (stroke, arc);
    return stroke;
}

juce::Path radial (juce::Point<float> centre, float r0, float r1, float angle, float width)
{
    juce::Path line;
    line.startNewSubPath (centre.getPointOnCircumference (r0, angle));
    line.lineTo (centre.getPointOnCircumference (r1, angle));
    juce::Path stroke;
    juce::PathStrokeType (width, juce::PathStrokeType::mitered, juce::PathStrokeType::butt).createStrokedPath (stroke, line);
    return stroke;
}

juce::Font labelFont()
{
    return uv::type::font (uv::tok::type::label);
}
} // namespace

/* ------------------------------------------------------------- control -- */

class RadialMenu::Control final : public juce::Component
{
public:
    Control (RadialMenu& m, int i) : menu (m), index (i)
    {
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (false);
    }

    const Item& item() const { return menu.items[(size_t) index]; }
    bool live() const { return menu.isLive (index); }

    bool hitTest (int x, int y) override
    {
        const auto p = getPosition().toFloat() + juce::Point<float> ((float) x, (float) y);
        return menu.itemAt (p) == index;
    }

    /* ---- the pointer */

    /* Where an event is in the menu's parent: what stays put if the menu is
     * placed again under a drag (its labels changed), so the drag is
     * measured from where it was pressed and nothing jumps. */
    juce::Point<float> inParent (const juce::MouseEvent& e) const
    {
        return e.position + getPosition().toFloat() + menu.getPosition().toFloat();
    }

    void mouseEnter (const juce::MouseEvent&) override
    {
        if (menu.hovered != index)
        {
            menu.hovered = index;
            menu.hotChanged();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (menu.hovered == index)
        {
            menu.hovered = -1;
            menu.hotChanged();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        focus.pointerUsed();
        drag.reset();
        if (! live() || ! e.mods.isLeftButtonDown())
            return;

        /* THE SENSITIVITY, THE DETENTS AND WHAT SHIFT MEANS ARE THE PRESS'S:
         * the value is measured from the press, and re-reading any of them
         * half-way would make it jump (Knob.cpp). */
        const auto& it = item();
        const bool shift = e.mods.isShiftDown();
        const bool alternate = shift && it.shiftIsAlternate;
        const bool fine = shift && ! it.shiftIsAlternate;
        drag = Drag { it.id, inParent (e), alternate ? it.alternate01 : it.value01,
                      alternate, fine, alternate ? std::vector<double> {} : it.detents, false, false };
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! drag.has_value())
            return;
        const auto moved = inParent (e) - drag->from;
        if (! drag->left)
        {
            if (std::abs (moved.x) < deadZone && std::abs (moved.y) < deadZone)
                return;
            drag->left = true;   // no click any more, on any item
        }
        if (! live() || item().kind != Item::Kind::value)
            return;
        if (! drag->begun)
        {
            drag->begun = true;
            menu.dragged = index;
            const auto id = drag->id;
            const bool alternate = drag->alternate;
            juce::Component::SafePointer<Control> self (this);
            menu.hotChanged();
            if (self == nullptr)
                return;
            if (menu.onBegin)
                menu.onBegin (id, alternate);
            if (self == nullptr || ! drag.has_value())
                return;
        }
        const double v = detents::dragValue (drag->start, (double) -moved.y,
                                             drag->fine ? travelPx * fineRatio : travelPx,
                                             drag->detents, drag->fine);
        if (menu.onInput)
            menu.onInput (drag->id, v, drag->alternate);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! drag.has_value())
            return;
        const auto was = *drag;
        drag.reset();
        if (was.begun)
        {
            menu.dragged = -1;
            juce::Component::SafePointer<Control> self (this);
            menu.hotChanged();
            if (self != nullptr && menu.onEnd)
                menu.onEnd (was.id);
            return;
        }
        /* A click: an action acts, if the pointer is still on it. */
        if (! was.left && live() && item().kind == Item::Kind::action && item().id == was.id
            && hitTest (e.getPosition().x, e.getPosition().y) && menu.onAction)
            menu.onAction (was.id);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (live() && item().kind == Item::Kind::value && menu.onReset)
            menu.onReset (item().id);
    }

    /* ---- the keyboard */
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.isKeyCode (juce::KeyPress::tabKey))
        {
            /* Tab goes round; Shift+Tab leaves, to the stop before the menu. */
            if (k.getModifiers().isShiftDown())
                menu.moveKeyboardFocusToSibling (false);
            else
                menu.move (index, 1);
            return true;
        }

        const auto key = keys::keyOf (k);
        if (key == keys::Key::escape)
        {
            if (! menu.onClose)
                return false;
            menu.onClose();
            return true;
        }
        if (key == keys::Key::left || key == keys::Key::right)
        {
            menu.move (index, key == keys::Key::right ? 1 : -1);
            return true;
        }
        if (key == keys::Key::enter)
        {
            if (! live() || item().kind != Item::Kind::action)
                return false;
            const auto id = item().id;
            juce::Component::SafePointer<Control> self (this);
            used();
            if (self != nullptr && menu.onAction)
                menu.onAction (id);
            return true;
        }

        if (! live() || item().kind != Item::Kind::value)
            return false;
        const auto& it = item();
        const bool shift = k.getModifiers().isShiftDown();
        const bool alternate = shift && it.shiftIsAlternate;
        const double from = alternate ? it.alternate01 : it.value01;

        std::optional<double> to;
        if (key == keys::Key::up || key == keys::Key::down)
        {
            const double step = shift && ! alternate ? (double) keys::fineStep : (double) keys::step;
            to = from + (key == keys::Key::up ? step : -step);
        }
        else if (key == keys::Key::pageUp || key == keys::Key::pageDown)
        {
            const int dir = key == keys::Key::pageUp ? 1 : -1;
            const auto next = alternate ? std::nullopt : detents::next (from, it.detents, dir);
            to = next.has_value() ? *next : from + dir * (double) keys::pageStep;
        }
        else if (key == keys::Key::home || key == keys::Key::end)
        {
            to = key == keys::Key::end ? 1.0 : 0.0;
        }
        if (! to.has_value())
            return false;

        const auto id = it.id;
        juce::Component::SafePointer<Control> self (this);
        used();
        if (self != nullptr)
            commit (id, juce::jlimit (0.0, 1.0, *to), alternate);
        return true;
    }

    /* One complete edit: what a key or a screen reader asks for. */
    void commit (juce::String id, double value, bool alternate)
    {
        juce::Component::SafePointer<Control> self (this);
        if (menu.onBegin)
            menu.onBegin (id, alternate);
        if (self == nullptr)
            return;
        if (menu.onInput)
            menu.onInput (id, value, alternate);
        if (self == nullptr)
            return;
        if (menu.onEnd)
            menu.onEnd (id);
    }

    void focusGained (FocusChangeType cause) override
    {
        focus.focusGained (cause);
        menu.keyboardItem = index;
        menu.hotChanged();
    }

    void focusLost (FocusChangeType) override
    {
        focus.focusLost();
        if (menu.keyboardItem == index)
            menu.keyboardItem = -1;
        menu.hotChanged();
    }

    /* A key was used on this item: its focus shows from now on. */
    void used()
    {
        focus.keyUsed();
        menu.keyboardItem = index;
        menu.hotChanged();
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    RadialMenu& menu;
    const int index;
    FocusVisibility focus { *this };

private:
    struct Drag
    {
        juce::String id;
        juce::Point<float> from;
        double start;
        bool alternate;
        bool fine;
        std::vector<double> detents;
        bool left;    // past the dead zone: a drag, not a click
        bool begun;   // the gesture is open
    };
    std::optional<Drag> drag;
};

namespace
{
/* A value item to a screen reader: the value 0..1 to move, the owner's text
 * to read. */
class ItemValue final : public juce::AccessibilityValueInterface
{
public:
    ItemValue (std::function<const RadialMenu::Item&()> it, std::function<void (double)> set)
        : item (std::move (it)), commit (std::move (set))
    {
    }

    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return item().value01; }
    juce::String getCurrentValueAsString() const override { return item().valueText; }
    void setValue (double v) override { commit (juce::jlimit (0.0, 1.0, v)); }
    void setValueAsString (const juce::String&) override {}
    AccessibleValueRange getRange() const override { return { { 0.0, 1.0 }, (double) keys::step }; }

private:
    std::function<const RadialMenu::Item&()> item;
    std::function<void (double)> commit;
};
} // namespace

std::unique_ptr<juce::AccessibilityHandler> RadialMenu::Control::createAccessibilityHandler()
{
    if (item().kind == Item::Kind::action)
    {
        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::menuItem,
            juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press, [this] {
                if (live() && menu.onAction)
                    menu.onAction (item().id);
            }));
    }
    return std::make_unique<juce::AccessibilityHandler> (
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces { std::make_unique<ItemValue> (
            [this]() -> const Item& { return item(); },
            [this] (double v) {
                if (live())
                    commit (item().id, v, false);
            }) });
}

/* ---------------------------------------------------------------- menu -- */

RadialMenu::RadialMenu()
{
    /* One Tab stop, which hands the keyboard to an item (focusGained); a
     * press on the plate does not take it. */
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setTitle ("Menu");
}

RadialMenu::~RadialMenu() = default;

void RadialMenu::setItems (std::vector<Item> next)
{
    jassert (next.size() <= (size_t) maxItems);   // the ring holds eight
    for ([[maybe_unused]] const auto& it : next)
        jassert (it.icon.isEmpty() || uv::hasIcon (it.icon));   // never a glyph outside the set

    for (auto& it : next)
    {
        it.detents = detents::clean (it.detents);
        it.value01 = juce::jlimit (0.0, 1.0, it.value01);
        it.alternate01 = juce::jlimit (0.0, 1.0, it.alternate01);
    }

    bool same = next.size() == items.size();
    for (size_t i = 0; same && i < next.size(); ++i)
        same = next[i].id == items[i].id && next[i].kind == items[i].kind;

    items = std::move (next);

    if (! same)
    {
        hovered = dragged = keyboardItem = -1;
        controls.clear();
        for (int i = 0; i < (int) items.size(); ++i)
        {
            controls.push_back (std::make_unique<Control> (*this, i));
            addAndMakeVisible (*controls.back());
        }
    }
    for (size_t i = 0; i < items.size(); ++i)
    {
        auto& control = *controls[i];
        control.setTitle (items[i].label);
        control.setEnabled (items[i].enabled);
        if (auto* handler = control.getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    }

    layout();
    hotChanged();
    repaint();
}

void RadialMenu::setCentreText (const juce::String& text)
{
    if (text == centreText)
        return;
    centreText = text;
    setTitle (text.isEmpty() ? juce::String ("Menu") : text);
    repaint();
}

void RadialMenu::placeAround (juce::Point<int> centre, juce::Rectangle<int> bounds)
{
    requestedAt = centre;
    within = bounds;
    placed = true;
    layout();
}

/* The plate and the chips round a ring centred at the origin. */
juce::Rectangle<int> RadialMenu::footprintAboutCentre() const
{
    const juce::Point<float> origin;
    auto area = juce::Rectangle<float> (2.0f * plateRadius, 2.0f * plateRadius).withCentre (origin);
    for (int i = 0; i < (int) items.size(); ++i)
        area = area.getUnion (chipAt (i, origin));
    return area.getSmallestIntegerContainer();
}

juce::Rectangle<int> RadialMenu::footprint() const
{
    return footprintAboutCentre() + placedAt;
}

void RadialMenu::layout()
{
    /* The footprint about the origin, then the centre moved as little as
     * keeps it inside `within` -- centred on it where it cannot fit. */
    const auto rel = footprintAboutCentre();
    auto at = requestedAt;
    if (placed)
    {
        const auto clampAxis = [] (int c, int lo, int hi, int boundLo, int boundHi) {
            if (hi - lo > boundHi - boundLo)
                return (boundLo + boundHi) / 2 - (lo + hi) / 2;
            return std::clamp (c, boundLo - lo, boundHi - hi);
        };
        at.x = clampAxis (at.x, rel.getX(), rel.getRight(), within.getX(), within.getRight());
        at.y = clampAxis (at.y, rel.getY(), rel.getBottom(), within.getY(), within.getBottom());
    }
    placedAt = at;

    setBounds ((rel + at).expanded (lightMargin));

    const auto centre = ringCentre();
    for (int i = 0; i < (int) controls.size(); ++i)
    {
        const auto box = juce::Rectangle<float> (2.0f * railRadius, 2.0f * railRadius).withCentre (itemCentre (i));
        controls[(size_t) i]->setBounds (box.getUnion (chipAt (i, centre)).getSmallestIntegerContainer());
    }
}

juce::Point<float> RadialMenu::ringCentre() const
{
    return (placedAt - getPosition()).toFloat();
}

juce::Point<float> RadialMenu::direction (int index) const
{
    const float angle = juce::MathConstants<float>::twoPi * (float) index / (float) std::max<size_t> (1, items.size());
    return { std::sin (angle), -std::cos (angle) };
}

juce::Point<float> RadialMenu::itemCentre (int index) const
{
    return ringCentre() + direction (index) * ringRadius;
}

juce::Rectangle<float> RadialMenu::chipAt (int index, juce::Point<float> centre) const
{
    const auto& style = uv::tok::type::label;
    const auto text = uv::type::cased (style, items[(size_t) index].label);
    if (text.isEmpty())
        return {};
    const float w = std::ceil (uv::type::width (labelFont(), text)) + 2.0f * chipPadX;
    const float h = style.lineHeight + 2.0f * chipPadY;
    const auto dir = direction (index);
    const auto anchor = centre + dir * (plateRadius + labelGap);

    const float x = dir.x > lean ? anchor.x : dir.x < -lean ? anchor.x - w : anchor.x - w * 0.5f;
    const float y = dir.y > lean ? anchor.y : dir.y < -lean ? anchor.y - h : anchor.y - h * 0.5f;
    return { x, y, w, h };
}

juce::Rectangle<float> RadialMenu::labelBounds (int index) const
{
    if (index < 0 || index >= (int) items.size())
        return {};
    return chipAt (index, ringCentre());
}

int RadialMenu::itemAt (juce::Point<float> p) const
{
    for (int i = 0; i < (int) items.size(); ++i)
        if (p.getDistanceFrom (itemCentre (i)) <= railRadius || labelBounds (i).contains (p))
            return i;
    return -1;
}

juce::Component& RadialMenu::item (int index)
{
    return *controls[(size_t) index];
}

bool RadialMenu::isLive (int index) const
{
    return isEnabled() && index >= 0 && index < (int) items.size() && items[(size_t) index].enabled;
}

int RadialMenu::getHot() const noexcept
{
    if (dragged >= 0)
        return dragged;
    if (hovered >= 0 && isLive (hovered))
        return hovered;
    if (keyboardItem >= 0 && keyboardItem < (int) controls.size() && controls[(size_t) keyboardItem]->focus.isVisible())
        return keyboardItem;
    return -1;
}

void RadialMenu::hotChanged()
{
    repaint();
    const int hot = getHot();
    if (hot == lastHot)
        return;
    lastHot = hot;
    if (onHint)
        onHint (hot >= 0 ? items[(size_t) hot].id : juce::String());
}

void RadialMenu::move (int from, int by)
{
    const int n = (int) items.size();
    for (int step = 1; step <= n; ++step)
    {
        const int to = ((from + by * step) % n + n) % n;
        if (! isLive (to))
            continue;
        auto& control = *controls[(size_t) to];
        keyboardItem = to;
        if (control.isShowing())
            control.grabKeyboardFocus();
        control.used();
        return;
    }
}

void RadialMenu::setCentrePassesThrough (bool on)
{
    centrePasses = on;
}

bool RadialMenu::hitTest (int x, int y)
{
    const juce::Point<float> p ((float) x, (float) y);
    const float r = p.getDistanceFrom (ringCentre());
    if (centrePasses && r < holeRadius)
        return false;
    return r <= plateRadius || itemAt (p) >= 0;
}

/* The menu's own focus is only on the way to an item's: the one the
 * keyboard was last on, else the first enabled one. */
void RadialMenu::focusGained (FocusChangeType)
{
    if (keyboardItem >= 0 && isLive (keyboardItem))
    {
        if (controls[(size_t) keyboardItem]->isShowing())
            controls[(size_t) keyboardItem]->grabKeyboardFocus();
        return;
    }
    move (-1, 1);
}

void RadialMenu::enablementChanged()
{
    hotChanged();
}

/* ---------------------------------------------------------------- paint -- */

void RadialMenu::paintItem (juce::Graphics& g, int index, bool hot) const
{
    const auto& it = items[(size_t) index];
    const bool live = isLive (index);
    const auto centre = itemCentre (index);
    const auto disc = juce::Rectangle<float> (discSize, discSize).withCentre (centre);

    /* The well. */
    g.setColour (hot && live ? c::bg300 : c::bg200);
    g.fillEllipse (disc);
    g.setColour (c::line100);
    g.drawEllipse (disc.reduced (hair * 0.5f), hair);

    if (it.kind == Item::Kind::value)
    {
        g.setColour (c::line200);
        g.fillPath (arcStroke (centre, startAngle, startAngle + sweep));

        for (const auto d : it.detents)
        {
            const bool on = std::abs (d - it.value01) < onDetent;
            g.setColour (on && live ? c::uv : c::inkDim);
            g.fillPath (radial (centre, tickFrom, tickTo, angleOf (d), hair));
        }

        const double origin = it.bipolar ? 0.5 : 0.0;
        if (std::abs (it.value01 - origin) > 0.0001)
        {
            const auto a = angleOf (origin), b = angleOf (it.value01);
            const auto arc = arcStroke (centre, std::min (a, b), std::max (a, b));
            if (live)
                uv::light::glowArc (g, arc);
            g.setColour (live ? c::uv : c::inkDim);
            g.fillPath (arc);
        }

        if (it.icon.isEmpty())
        {
            g.setColour (live ? c::uv : c::inkDim);
            g.fillPath (radial (centre, pointerFrom, pointerTo, angleOf (it.value01), uv::tok::stroke::strokeRail));
        }
    }

    if (it.icon.isNotEmpty())
        uv::drawIcon (g, it.icon, live ? c::ink : c::inkDim, disc);

    if (const auto chip = labelBounds (index); ! chip.isEmpty())
    {
        g.setColour (c::bg100);
        g.fillRoundedRectangle (chip, uv::tok::radius::radius1);
        const auto& style = uv::tok::type::label;
        uv::type::draw (g, uv::type::cased (style, it.label), chip, labelFont(),
                        ! live ? c::inkDim : hot ? c::ink : c::inkMuted, juce::Justification::centred);
    }
}

void RadialMenu::paint (juce::Graphics& g)
{
    const auto centre = ringCentre();
    const auto plate = juce::Rectangle<float> (2.0f * plateRadius, 2.0f * plateRadius).withCentre (centre);
    g.setColour (c::bg100);
    g.fillEllipse (plate);
    g.setColour (c::line200);
    g.drawEllipse (plate.reduced (hair * 0.5f), hair);

    const int hot = getHot();
    for (int i = 0; i < (int) items.size(); ++i)
        paintItem (g, i, i == hot);

    /* The middle: the hot value's text, else the owner's words. */
    const auto& valueStyle = uv::tok::type::value;
    const auto line = juce::Rectangle<float> (centreWidth, valueStyle.lineHeight).withCentre (centre);
    if (hot >= 0 && items[(size_t) hot].kind == Item::Kind::value && items[(size_t) hot].valueText.isNotEmpty())
        uv::type::drawValueWithUnit (g, items[(size_t) hot].valueText, line, isLive (hot));
    else if (centreText.isNotEmpty())
        uv::type::draw (g, centreText, line, uv::type::font (valueStyle), isEnabled() ? c::inkMuted : c::inkDim,
                        juce::Justification::centred);

    /* Keyboard focus last, over the neighbours: it is light. */
    for (int i = 0; i < (int) controls.size(); ++i)
        if (controls[(size_t) i]->focus.isVisible())
            uv::light::glowFocus (g, juce::Rectangle<float> (itemBox, itemBox).withCentre (itemCentre (i)), itemBox * 0.5f);
}

std::unique_ptr<juce::AccessibilityHandler> RadialMenu::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::popupMenu);
}

} // namespace ni::ui

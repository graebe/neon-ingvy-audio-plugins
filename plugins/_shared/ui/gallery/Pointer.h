// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A pointer and a keyboard made of calls: the events a person would give a
 * control, given to it directly.
 *
 * FOR THE GALLERY AND THE TESTS, NOT THE KIT. A gallery page shows a state
 * the pointer would give -- hover, pressed -- by giving the control the event
 * that makes it, and a test drives a control the way a person does. Neither
 * opens a window (the snapshot harness never does), so JUCE's own mouse
 * machinery, which needs one, is not there to deliver them: this calls the
 * control's mouseDown, mouseDrag, mouseUp ... itself, with the events JUCE
 * would have built, from the left button unless told otherwise.
 *
 *   ni::ui::gallery::Pointer p;
 *   p.down (knob, { 24, 24 }, juce::ModifierKeys::shiftModifier);
 *   p.drag ({ 24, 4 });          // 20px up, in the knob's coordinates
 *   p.up();
 *
 * What it cannot do is what needs the window: which component is under the
 * pointer, and keyboard focus. A test gives focus to a control directly
 * (focusGained), as the info tests do.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui::gallery
{

class Pointer
{
public:
    /* An event at `at` in `c`'s coordinates, as JUCE would build it. */
    static juce::MouseEvent event (juce::Component& c, juce::Point<float> at,
                                   juce::ModifierKeys mods = {}, int clicks = 1,
                                   juce::Point<float> downAt = {}, bool dragged = false)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods,
                                 juce::MouseInputSource::defaultPressure,
                                 juce::MouseInputSource::defaultOrientation,
                                 juce::MouseInputSource::defaultRotation,
                                 juce::MouseInputSource::defaultTiltX,
                                 juce::MouseInputSource::defaultTiltY,
                                 &c, &c, now, downAt, now, clicks, dragged);
    }

    /* The pointer arrives over `c`, moves over it, leaves it. */
    void enter (juce::Component& c, juce::Point<float> at = {}) { c.mouseEnter (event (c, at)); }
    void move (juce::Component& c, juce::Point<float> at) { c.mouseMove (event (c, at)); }
    void exit (juce::Component& c, juce::Point<float> at = { -1.0f, -1.0f }) { c.mouseExit (event (c, at)); }

    /* A press on `c`: the left button with `mods` added. Every drag and the
     * release go to `c` until up(), wherever the pointer is, as JUCE sends
     * them to the component that was pressed. */
    void down (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods = {}, int clicks = 1)
    {
        target = &c;
        downAt = at;
        position = at;
        held = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
        dragged = false;
        count = clicks;
        c.mouseDown (event (c, at, held, count, downAt, false));
    }

    void drag (juce::Point<float> to)
    {
        jassert (target != nullptr);   // down() first
        if (target == nullptr)
            return;
        position = to;
        dragged = dragged || to != downAt;
        target->mouseDrag (event (*target, to, held, count, downAt, dragged));
    }

    /* The release, where the pointer is now (or at `at`). */
    void up() { up (position); }
    void up (juce::Point<float> at)
    {
        jassert (target != nullptr);
        if (target == nullptr)
            return;
        auto* c = target;
        target = nullptr;
        c->mouseUp (event (*c, at, held, count, downAt, dragged || at != downAt));
    }

    /* A click: down and up where it was pressed. */
    void click (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods = {})
    {
        down (c, at, mods);
        up (at);
    }

    /* Two clicks, then the double-click JUCE sends after the second. */
    void doubleClick (juce::Component& c, juce::Point<float> at)
    {
        click (c, at);
        down (c, at, {}, 2);
        up (at);
        c.mouseDoubleClick (event (c, at, {}, 2, at, false));
    }

    /* The wheel over `c`: `dy` up is positive, as a mouse reports it. */
    static void wheel (juce::Component& c, juce::Point<float> at, float dy)
    {
        juce::MouseWheelDetails w {};
        w.deltaY = dy;
        c.mouseWheelMove (event (c, at), w);
    }

private:
    juce::Component* target = nullptr;
    juce::Point<float> downAt, position;
    juce::ModifierKeys held;
    bool dragged = false;
    int count = 1;
};

/* A key, as JUCE gives it to the focused control. */
inline bool key (juce::Component& c, int code, juce::ModifierKeys mods = {})
{
    return c.keyPressed (juce::KeyPress (code, mods, 0));
}

} // namespace ni::ui::gallery

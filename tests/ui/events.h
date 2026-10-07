// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Pointer and keyboard input for the display components' tests, made the way
 * JUCE makes it and handed to a component's own handlers.
 *
 * WHY BY HAND. The test programs open no window (main.cpp), so there is no
 * peer to post a real click to; a juce::MouseEvent built here and passed to
 * mouseDown / mouseUp / mouseMove is exactly what the peer would have
 * delivered, with the same source, modifiers, click count and positions.
 * The control under test cannot tell the difference, which is the point.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace ni::ui::test
{

/* A pointer event at `at` (c's coordinates), with the button and keys in
 * `mods`, pressed at `downAt`, `clicks` deep. */
inline juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> at,
                                 juce::ModifierKeys mods = {}, int clicks = 1,
                                 std::optional<juce::Point<float>> downAt = std::nullopt,
                                 bool dragged = false)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods,
                             juce::MouseInputSource::defaultPressure,
                             juce::MouseInputSource::defaultOrientation,
                             juce::MouseInputSource::defaultRotation,
                             juce::MouseInputSource::defaultTiltX,
                             juce::MouseInputSource::defaultTiltY,
                             &c, &c, now, downAt.value_or (at), now, clicks, dragged);
}

/* A click at `at`: the press with the left button held, the release without. */
inline void click (juce::Component& c, juce::Point<float> at, juce::ModifierKeys keys = {})
{
    c.mouseDown (mouseAt (c, at, keys.withFlags (juce::ModifierKeys::leftButtonModifier)));
    c.mouseUp (mouseAt (c, at, keys));
}

/* The centre of a component, in its own coordinates. */
inline juce::Point<float> centreOf (const juce::Component& c)
{
    return c.getLocalBounds().toFloat().getCentre();
}

} // namespace ni::ui::test

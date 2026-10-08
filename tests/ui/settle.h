// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The message loop, run until what was posted has been delivered.
 *
 *   field->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
 *   ni::ui::test::settle();          // the TextEditor's Enter has arrived
 *
 * A TextEditor posts its Enter, Escape and lost focus to itself; a
 * ParameterAttachment posts a change made on another thread. Running the loop
 * for some milliseconds and hoping they came is a bet on the machine: a busy
 * one runs nothing at all in ten. So settle() posts a marker of its own and
 * runs the loop until the marker arrives -- the queue is first in, first out,
 * so everything posted before it has arrived too. The bound only catches a
 * loop that has stopped delivering.
 *
 * It waits for messages, never for a timer: a test that needs a timer's work
 * calls what the timer calls.
 */
#pragma once

#include <juce_events/juce_events.h>

#include <doctest.h>

#include <memory>

namespace ni::ui::test
{

inline void settle()
{
    auto delivered = std::make_shared<bool> (false);
    juce::MessageManager::callAsync ([delivered] { *delivered = true; });

    const auto until = juce::Time::getMillisecondCounter() + 10000;
    while (! *delivered && juce::Time::getMillisecondCounter() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    REQUIRE (*delivered);
}

} // namespace ni::ui::test

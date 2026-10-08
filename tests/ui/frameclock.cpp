// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The frame clock: one tick for every subscriber, running only while anyone
 * listens, and standing in with a timer when the display sends no vblank --
 * which, with no window at all, is always.
 */
#include "FrameClock.h"

#include <doctest.h>

#include <algorithm>
#include <vector>

using ni::ui::FrameClock;

TEST_CASE ("frame clock: every subscriber is called once per tick, in order")
{
    juce::Component root;
    double now = 1000.0;
    FrameClock clock (root, [&] { return now; });

    std::vector<juce::String> calls;
    auto a = clock.subscribe ([&] (double t) { calls.push_back ("a@" + juce::String (t)); });
    auto b = clock.subscribe ([&] (double) { calls.push_back ("b"); });

    clock.tick();
    now = 1016.0;
    clock.tick();

    CHECK (calls == std::vector<juce::String> { "a@1000", "b", "a@1016", "b" });
}

TEST_CASE ("frame clock: it runs only while something listens")
{
    juce::Component root;
    FrameClock clock (root);
    CHECK_FALSE (clock.isRunning());

    {
        auto sub = clock.subscribe ([] (double) {});
        CHECK (clock.isRunning());
        CHECK (sub.isActive());
    }
    CHECK_FALSE (clock.isRunning());   // the subscription's end is the stopping
}

TEST_CASE ("frame clock: a subscriber may drop itself, or another, during a tick")
{
    juce::Component root;
    FrameClock clock (root);

    int aCalls = 0, bCalls = 0;
    FrameClock::Subscription a, b;
    a = clock.subscribe ([&] (double) { ++aCalls; a.reset(); b.reset(); });
    b = clock.subscribe ([&] (double) { ++bCalls; });

    clock.tick();
    clock.tick();

    CHECK (aCalls == 1);
    CHECK (bCalls == 0);   // dropped before its turn: not called this frame either
    CHECK_FALSE (clock.isRunning());
}

TEST_CASE ("frame clock: a subscription outliving its clock is harmless")
{
    FrameClock::Subscription sub;
    {
        juce::Component root;
        FrameClock clock (root);
        sub = clock.subscribe ([] (double) {});
        CHECK (sub.isActive());
    }
    CHECK_FALSE (sub.isActive());
    sub.reset();   // nothing to unsubscribe from, and no crash
}

TEST_CASE ("frame clock: with no vblank, the timer stands in and the frames still come")
{
    juce::Component root;   // no window, so no display, so no vblank
    FrameClock clock (root);
    CHECK_FALSE (clock.isOnVBlank());

    std::vector<double> frames;
    auto sub = clock.subscribe ([&] (double t) { frames.push_back (t); });

    /* The timer is the message loop's, and how many of its callbacks a busy
     * machine delivers in a given time is the machine's business, not the
     * clock's: a CI runner gave two in a fifth of a second. So the loop runs
     * until three frames have come, or ten seconds have gone -- a bound only
     * a stopped timer reaches -- and what is checked is that they came, one
     * after another, from the stand-in. */
    const auto until = juce::Time::getMillisecondCounter() + 10000;
    while (frames.size() < 3 && juce::Time::getMillisecondCounter() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    REQUIRE (frames.size() >= 3);
    CHECK (std::is_sorted (frames.begin(), frames.end()));
    CHECK (frames.front() < frames.back());   // frames, not one frame repeated
    CHECK_FALSE (clock.isOnVBlank());
}

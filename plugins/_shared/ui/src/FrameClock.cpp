// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * One tick per frame. FrameClock.h says why it is never needed for anything
 * but a repaint.
 */
#include "FrameClock.h"

#include <algorithm>

namespace ni::ui
{

FrameClock::FrameClock (juce::Component& root, Clock now)
    : clock (now ? std::move (now) : Clock ([] { return juce::Time::getMillisecondCounterHiRes(); })),
      self (std::make_shared<FrameClock*> (this)),
      vblank (&root, [this] (double) { onVBlank(); })
{
}

FrameClock::~FrameClock()
{
    stopTimer();
}

FrameClock::Subscription FrameClock::subscribe (Callback callback)
{
    JUCE_ASSERT_MESSAGE_THREAD
    jassert (callback != nullptr);

    const int id = nextId++;
    subscribers.push_back ({ id, std::move (callback) });
    update();
    return Subscription (self, id);
}

void FrameClock::unsubscribe (int id)
{
    subscribers.erase (std::remove_if (subscribers.begin(), subscribers.end(),
                                       [id] (const Subscriber& s) { return s.id == id; }),
                       subscribers.end());
    update();
}

void FrameClock::update()
{
    /* The stand-in runs while anyone listens, and only checks whether the
     * display has been ticking; with nobody listening, nothing runs. */
    if (subscribers.empty())
        stopTimer();
    else if (! isTimerRunning())
        startTimerHz (fallbackHz);
}

double FrameClock::now() const
{
    return clock();
}

bool FrameClock::isOnVBlank() const
{
    return now() - lastVBlank <= fallbackAfterMs;
}

void FrameClock::onVBlank()
{
    lastVBlank = now();
    if (isRunning())
        tick();
}

void FrameClock::timerCallback()
{
    if (! isOnVBlank())
        tick();
}

void FrameClock::tick()
{
    JUCE_ASSERT_MESSAGE_THREAD
    const double t = now();
    lastTick = t;

    /* By id, over a copy: a callback may drop its own subscription, or
     * another's, or add one; a dropped one is not called again this frame,
     * and an added one starts with the next. */
    std::vector<int> ids;
    ids.reserve (subscribers.size());
    for (const auto& s : subscribers)
        ids.push_back (s.id);

    for (const int id : ids)
    {
        const auto it = std::find_if (subscribers.begin(), subscribers.end(),
                                      [id] (const Subscriber& s) { return s.id == id; });
        if (it == subscribers.end())
            continue;
        const auto callback = it->callback;   // the subscription may go during the call
        callback (t);
    }
}

/* ======================================================== subscription == */

FrameClock::Subscription::Subscription (std::weak_ptr<FrameClock*> c, int i)
    : clock (std::move (c)), id (i)
{
}

FrameClock::Subscription::Subscription (Subscription&& other) noexcept
    : clock (std::move (other.clock)), id (other.id)
{
    other.id = 0;
}

FrameClock::Subscription& FrameClock::Subscription::operator= (Subscription&& other) noexcept
{
    if (this != &other)
    {
        reset();
        clock = std::move (other.clock);
        id = other.id;
        other.id = 0;
    }
    return *this;
}

FrameClock::Subscription::~Subscription()
{
    reset();
}

void FrameClock::Subscription::reset()
{
    if (id != 0)
        if (const auto owner = clock.lock())
            (*owner)->unsubscribe (id);
    clock.reset();
    id = 0;
}

bool FrameClock::Subscription::isActive() const noexcept
{
    return id != 0 && ! clock.expired();
}

} // namespace ni::ui

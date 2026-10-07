// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * One display-rate tick for everything in an editor that moves on its own --
 * a playhead, a meter, the spectrogram's next column, the Ground -- so they
 * redraw together, once per frame, from one source.
 *
 * NEVER NEEDED FOR CORRECTNESS. This is the rule the web editors were rebuilt
 * around, after a host that took its plugin window for hidden stopped their
 * frame callbacks and their timers: no behaviour may hang off a display
 * frame. State lives in the model (the engine's snapshots, read on the message
 * thread when wanted) and in the controls; a tick only asks "has it moved?"
 * and repaints. A tick missed, late or never coming leaves a picture out of
 * date and nothing wrong: the next paint of any kind draws the truth.
 *
 * TWO SOURCES, ONE TICK. juce::VBlankAttachment on the root is the source while
 * the window is on a display; a juce::Timer at fallbackHz stands in whenever
 * no vblank has come for fallbackAfterMs -- no peer yet, a host or platform
 * that does not deliver them, a window between displays. Subscribers see one
 * tick either way, at most one per vblank.
 *
 * IT RUNS ONLY WHILE SOMETHING LISTENS. A Subscription is the listening, and
 * dropping it is the stopping: the Ground holds one while its field moves and
 * lets it go at rest; a playhead holds one while the transport plays. With no
 * subscribers the timer is stopped and vblanks are ignored.
 *
 *   ni::ui::FrameClock clock { *this };                 // in the editor's root
 *   auto sub = clock.subscribe ([this] (double ms) { if (moved()) repaint(); });
 *
 * Message thread only. tick() is public so a test can drive frames by hand,
 * and the clock's `now` can be injected for the same reason.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace ni::ui
{

class FrameClock final : private juce::Timer
{
public:
    /* The stand-in's rate, and how long without a vblank before it stands in. */
    static constexpr int fallbackHz = 60;
    static constexpr double fallbackAfterMs = 100.0;

    /* Milliseconds, monotonic. */
    using Clock = std::function<double()>;
    /* Called once per frame with the frame's time, in ms of Clock. */
    using Callback = std::function<void (double nowMs)>;

    /* `root` is the component whose display sets the frame rate: the
     * editor's root. It must outlive the clock. */
    explicit FrameClock (juce::Component& root, Clock now = {});
    ~FrameClock() override;

    /* A subscriber's place in the clock; destroying it unsubscribes, from
     * anywhere, even from inside its own callback. */
    class Subscription
    {
    public:
        Subscription() = default;
        Subscription (Subscription&&) noexcept;
        Subscription& operator= (Subscription&&) noexcept;
        ~Subscription();

        void reset();
        bool isActive() const noexcept;

    private:
        friend class FrameClock;
        Subscription (std::weak_ptr<FrameClock*> clock, int id);

        std::weak_ptr<FrameClock*> clock;
        int id = 0;
    };

    [[nodiscard]] Subscription subscribe (Callback);

    /* One frame: every subscriber called once, in the order they subscribed.
     * What the vblank and the stand-in timer call; a test calls it directly. */
    void tick();

    /* The clock's time now, in ms. */
    double now() const;

    /* Anyone listening. */
    bool isRunning() const noexcept { return ! subscribers.empty(); }

    /* Whether the frames are the display's (a vblank within fallbackAfterMs)
     * rather than the stand-in's. */
    bool isOnVBlank() const;

private:
    void unsubscribe (int id);
    void update();
    void onVBlank();
    void timerCallback() override;

    struct Subscriber
    {
        int id;
        Callback callback;
    };

    Clock clock;
    std::vector<Subscriber> subscribers;
    int nextId = 1;
    double lastVBlank = -1.0e9;
    double lastTick = -1.0e9;

    /* What a Subscription holds: a way back to this clock that knows when
     * the clock is gone. */
    std::shared_ptr<FrameClock*> self;

    juce::VBlankAttachment vblank;

    JUCE_DECLARE_NON_COPYABLE (FrameClock)
};

} // namespace ni::ui

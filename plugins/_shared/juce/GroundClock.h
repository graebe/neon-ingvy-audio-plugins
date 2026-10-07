// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor's Ground, as a plugin keeps it: the beat clock (ground.h, gnd_*)
 * ticked by the host's transport on the audio thread, and its rings handed to
 * the editor on the message thread (EditorModel::takeRings).
 *
 * WHAT RINGS IS THE ENGINE'S, not the shell's: ground-core counts a downbeat
 * and the other quarter notes from the host's position, tempo and meter, and
 * a position the host cannot give rings nothing. The clock runs only while an
 * editor is open (setActive), and a reopened editor starts afresh rather than
 * ringing a backlog.
 *
 * HEADER-ONLY AND OPT-IN. Every product whose Rust engine carries the ground's
 * C ABI (one archive per plugin: ground-capi is compiled into it) includes
 * this; a product without one never compiles a call to it.
 */
#pragma once

#include "Processor.h"
#include "ground.h"

#include <cstdint>

namespace ni
{

class GroundClock
{
public:
    GroundClock() : clock (gnd_new (44100.0)) {}
    ~GroundClock() { gnd_free (clock); }

    /* The host's rate, and a fresh start. Any thread. */
    void prepare (double sampleRate)
    {
        gnd_set_sample_rate (clock, sampleRate);
        gnd_reset (clock);
    }

    /* While an editor shows it. On, the rings counted so far are forgotten,
     * so a reopened editor plays none from before it opened. Message thread. */
    void setActive (bool on)
    {
        if (on)
            seen = gnd_fires (clock);
        gnd_set_active (clock, on ? 1 : 0);
    }

    /* One block of the host's transport. The audio thread; allocates nothing. */
    void tick (const HostClock& c, int frames)
    {
        gnd_tick (clock, c.ppq, c.bpm, c.numerator, c.denominator, c.known && c.playing ? 1 : 0, frames);
    }

    /*
     * The rings since the last call, at most `capacity`: the clock keeps a
     * count and the latest strength, so a burst between two frames is the
     * count, each at the latest strength -- the editor plays them as one
     * frame's worth. Message thread.
     */
    int takeRings (float* strengths, int capacity)
    {
        const std::uint32_t fires = gnd_fires (clock);
        const std::uint32_t fresh = fires - seen;
        seen = fires;
        if (fresh == 0 || capacity <= 0 || strengths == nullptr)
            return 0;
        const int n = (int) (fresh < (std::uint32_t) capacity ? fresh : (std::uint32_t) capacity);
        const float strength = gnd_strength (clock);
        for (int i = 0; i < n; ++i)
            strengths[i] = strength;
        return n;
    }

private:
    gnd_t* const clock;
    std::uint32_t seen = 0;

    GroundClock (const GroundClock&) = delete;
    GroundClock& operator= (const GroundClock&) = delete;
};

} // namespace ni

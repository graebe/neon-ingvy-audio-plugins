// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE KICK BEHIND THE DUCK: what the plot shows of the signal the user ducks
 * against. It never touches the audio or the trigger; it is a picture.
 *
 * Three choices, and the set keeps which (a string in the state, Params.h):
 *
 *   off          nothing
 *   key          the host's sidechain key, already this block's own samples,
 *                filed under the sweep beside the dry and the wet
 *   bus n        NI Listen-In's bus n, from another track: the kick tap
 *                (sc_kick.h) files each of its frames under the sweep this
 *                track had at the same timeline sample, whichever track the
 *                host ran first
 *
 * THE THREADS, as Listen-In's pusher has them, the other way round: the
 * message thread opens the bus's reader and lends it to the audio thread
 * through a handoff (shell_handoff.h), which frees a replaced one only once no
 * block holds it. `service` is the message thread's -- the editor's model
 * calls it every frame, and nothing needs a reader while no window shows the
 * plot. `prepare` is the host's prepare; `file` the audio thread's, between
 * the engine's tap and the next chunk, allocating nothing.
 */
#pragma once

#include "ni/Band.h"
#include "sc_kick.h"
#include "shell_handoff.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ni::sc
{

/* A kick choice as one int: 0 off, -1 the key, 1..16 a bus. */
namespace kick
{
inline constexpr int off = 0;
inline constexpr int key = -1;

/* As the set keeps it: "" off, "key", "bus:<n>". */
std::string toText (int choice);
/* Anything this build did not write is off. */
int fromText (const std::string&);
} // namespace kick

class Kick
{
public:
    /* The scope's columns (SideChain.h asserts they agree). */
    static constexpr int columns = 512;
    using Capture = ni::Band<columns>;

    Kick();
    ~Kick();

    /* ---- the message thread */

    /* The choice, and whether it changed. A change clears the picture. Any
     * thread may set it (a state loads on the host's), the next service acts
     * on it. */
    bool choose (int choice);
    int choice() const noexcept { return wanted.load (std::memory_order_relaxed); }

    /* Open, swap or drop the reader the choice needs, and free a replaced
     * one. `fresh`: start from the bus's live edge (a window opened). */
    void service (bool fresh = false);

    /* What the kick in the picture is, as sc_kick.h's SC_KICK_*; the key is
     * SC_KICK_ALIGNED while the host connects one, SC_KICK_WAITING until. */
    int status (bool keyConnected) const;

    /* ---- the host's prepare, while no block runs */

    void prepare (std::uint32_t sampleRate, int maximumBlock);

    /* ---- the audio thread, one chunk of `frames` after the engine's tap */

    void file (const float* sweep, int frames, bool timed, std::int64_t timeline, const float* keyL,
               const float* keyR);

    const Capture& capture() const noexcept { return band; }
    Capture& capture() noexcept { return band; }

private:
    struct TapDeleter
    {
        void operator() (sc_kick_t* k) const noexcept { sc_kick_destroy (k); }
    };
    std::unique_ptr<sc_kick_t, TapDeleter> tap;
    shell_handoff_t* handoff = nullptr;
    Capture band;
    std::atomic<int> wanted { kick::off };

    /* The message thread's: the slot whose reader is lent, and when the last
     * open was tried. */
    int openSlot = 0;
    bool hasReader = false;
    std::uint32_t lastTry = 0;

    /* The audio thread's: a chunk's kick and its sweeps. */
    std::vector<float> x, at;

    JUCE_DECLARE_NON_COPYABLE (Kick)
};

} // namespace ni::sc

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What NI Spectrogram's session remembers, and the chunk a set keeps it in.
 *
 * NO PARAMETERS. Nothing here changes what comes out of the plugin; what a
 * user sets up -- the buses, the view, the comparison, the clash criteria and
 * the zoom -- is the SESSION, saved with the host's state as the iPlug2 build
 * saved it: tests/fixtures/iplug2/FORMAT.md's Spectrogram, five strings after
 * the "NIst" header (ni::nist, layout()), the first required, the rest each
 * optional because they were added one at a time.
 *
 * WHAT THE SESSION IS LOOKING AT, BETWEEN THREADS (Session). A host calls the
 * state on a thread of its choosing -- auval's stress test on one of its own,
 * some DAWs on a loader thread -- while the editor's commands and the
 * receiver's service run on the message thread. The receiver's source changes
 * are the message thread's (spectro_recv.h): they open readers and wait for
 * the analysis thread. So a load does not touch the receiver. It records what
 * it read here, and the message thread's next Service applies it; a save
 * reads it here too, so a save straight after a load writes the load even
 * before it has been applied.
 *
 *   any thread but audio     Get, Load, Edit, revision
 *   message thread           Service, Applied
 *
 * The lock is held only to copy Fields in or out, never across a receiver
 * call and never on the audio thread, which sees none of this.
 *
 * Plain C++ and no JUCE, so its tests link it alone.
 */
#pragma once

#include "Nist.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace spectro::state
{

/* What a session remembers. Channel 0 is the track the plugin sits on; n > 0
 * the n-th bus opened, in the order `sources` asks for them. */
struct Fields
{
    /* The Listen-In buses listened to, 1-based slots, in order. */
    std::vector<unsigned int> sources;
    float clashFloorDb = -60.0f;
    float clashBalanceDb = 12.0f;
    /* The channels added into the picture; never empty once loaded. */
    std::vector<int> view { 0 };
    int cmpA = 0;
    int cmpB = 1;
    bool clashOn = false;
    /* The zoom, in Hz. */
    float rangeLo = 10.0f;
    float rangeHi = 20000.0f;

    bool operator== (const Fields&) const;
    bool operator!= (const Fields& o) const { return ! (*this == o); }
};

/* FORMAT.md's Spectrogram: no parameters, five strings, the first required. */
const ni::nist::Layout& layout();

/* The five strings, as the iPlug2 build wrote them: "2,5", "-60.00:12.00",
 * "0,1,2", "1:2:1", "40.00:800.00". '.' whatever the locale. */
std::vector<std::string> strings (const Fields&);

/*
 * The strings a chunk carried, over `f`: a string the chunk does not carry,
 * or one that does not read, keeps `f`'s value. False -- and `f` untouched --
 * when the first is missing or not digits and commas: every build that wrote
 * anything wrote it so, and a chunk it cannot be read from is not one of ours.
 */
bool apply (const std::vector<std::string>& strings, Fields& f);

/* A whole state: the session, and the host's bypass after it. */
std::vector<std::uint8_t> write (const Fields&, bool bypass);

struct Loaded
{
    Fields fields;
    /* The wrapper's flag after the chunk, when the stream carries one. */
    std::optional<bool> bypass;
};

/* What a stream holds, over `current`, or nothing when no build wrote it. */
std::optional<Loaded> read (const void* data, std::size_t size, const Fields& current);

class Session
{
public:
    /* The receiver calls a change needs, made by Service on the message
     * thread. The processor binds them to srecv_*; a test records them. */
    struct Sink
    {
        virtual ~Sink() = default;
        virtual void applySources (const std::vector<unsigned int>& slots) = 0;
        virtual void applyClash (float floorDb, float balanceDb) = 0;
        virtual void applyRange (float lo, float hi) = 0;
    };

    /* `initial` is what the receiver was built with: applied from the start. */
    explicit Session (const Fields& initial = {});

    /* What the session holds, a change not yet applied included. */
    Fields get() const;
    /* Moves on every load and edit, so a reader copies get() only when it
     * changed. Any thread; lock-free. */
    std::uint32_t revision() const noexcept { return rev.load (std::memory_order_acquire); }

    /* A state read into the session, as one step: what the stream does not
     * carry keeps the value it had at this moment. Returns the stream's bypass
     * flag through `bypass`, and false -- marking nothing -- for a stream no
     * build wrote. */
    bool load (const void* data, std::size_t size, std::optional<bool>& bypass);
    /* A change from the editor, for Service to apply. */
    void edit (const std::function<void (Fields&)>& change);

    /*
     * Message thread. Apply what changed since the last Service -- or, with
     * `all`, everything, for a receiver just built -- through `sink`, one call
     * per part that moved. Returns true when what it applied came from a load.
     */
    bool service (Sink& sink, bool all = false);
    /* Message thread. What the receiver has been given. */
    const Fields& applied() const noexcept { return done; }

private:
    mutable std::mutex lock;
    Fields wanted;         /* under lock */
    bool changed = false;  /* under lock */
    bool loaded = false;   /* under lock */
    std::atomic<std::uint32_t> rev { 0 };
    Fields done;           /* the message thread's */
};

} // namespace spectro::state

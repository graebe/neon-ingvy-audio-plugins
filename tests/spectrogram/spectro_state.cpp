// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Spectrogram's saved session, on its own: State.cpp's chunk and its
 * Session, with no JUCE and no plugin around them.
 *
 *   the chunk     what the iPlug2 build wrote (tests/fixtures/iplug2/FORMAT.md):
 *                 the two fixtures decode to their sessions and are written
 *                 back byte for byte; a session saved, loaded and saved again
 *                 is the same bytes; a chunk from before the header, or one
 *                 that ends after any of its strings, opens; what no build
 *                 wrote is refused and changes nothing
 *   the Session   a host's state calls on a thread of their own, the message
 *                 thread servicing the receiver beside them -- what auval
 *                 -stress did to the iPlug2 build, and what once left one of
 *                 the two waiting forever. The processor's readState and
 *                 writeState are Session::load and Session::get, and its
 *                 receiver calls are the Sink, so this is the processor's
 *                 path without the processor.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "State.h"
#include "spectro_recv.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace st = spectro::state;
using Bytes = std::vector<std::uint8_t>;

namespace
{

Bytes fixture (const char* scenario)
{
    std::ifstream in (std::string (NI_FIXTURES) + "/NISpectrogram/" + scenario + ".component.bin",
                      std::ios::binary);
    REQUIRE (in.good());
    return Bytes (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
}

/* A chunk as the current build writes it, read back over the defaults. */
std::optional<st::Loaded> reread (const Bytes& b)
{
    return st::read (b.data(), b.size(), st::Fields {});
}

/* The i-th of a run of distinct sessions, every field moved. */
st::Fields variant (int i)
{
    st::Fields f;
    f.sources = { unsigned (1 + i % 16), unsigned (1 + (i * 7 + 3) % 16) };
    f.clashFloorDb = -90.0f + float (i % 60);
    f.clashBalanceDb = 1.0f + float (i % 20);
    f.view = { 0, 1 + i % 3 };
    f.cmpA = i % 2;
    f.cmpB = 2 + i % 2;
    f.clashOn = (i % 2) != 0;
    f.rangeLo = 20.0f + float (i);
    f.rangeHi = 5000.0f + float (i);
    return f;
}

/* The body of a headerless chunk: the strings, int32 length and bytes. */
void putString (Bytes& out, const std::string& s)
{
    const auto n = (std::int32_t) s.size();
    const auto* p = reinterpret_cast<const std::uint8_t*> (&n);
    out.insert (out.end(), p, p + 4);
    out.insert (out.end(), s.begin(), s.end());
}

} // namespace

/* ---------------------------------------------------------------- chunk -- */

TEST_CASE ("a fresh session's strings are FORMAT.md's defaults")
{
    CHECK (st::strings (st::Fields {})
           == std::vector<std::string> { "", "-60.00:12.00", "0", "0:1:0", "10.00:20000.00" });
}

TEST_CASE ("both iPlug2 fixtures decode to their sessions and are written back byte for byte")
{
    const auto fresh = fixture ("default");
    auto got = reread (fresh);
    REQUIRE (got.has_value());
    CHECK (got->fields == st::Fields {});
    CHECK (got->bypass == std::optional<bool> (false));
    CHECK (st::write (got->fields, false) == fresh);

    /* The editor's Bass zoom with buses 2 and 5 beside this track, the clash
     * between them (session.json). */
    const auto session = fixture ("session");
    got = reread (session);
    REQUIRE (got.has_value());
    st::Fields want;
    want.sources = { 2, 5 };
    want.view = { 0, 1, 2 };
    want.cmpA = 1;
    want.cmpB = 2;
    want.clashOn = true;
    want.rangeLo = 40.0f;
    want.rangeHi = 800.0f;
    CHECK (got->fields == want);
    CHECK (st::write (got->fields, false) == session);
}

TEST_CASE ("a session: save, load, save again -- the same bytes, the bypass with them")
{
    st::Fields f;
    f.sources = { 3, 4, 9 };
    f.clashFloorDb = -48.5f;
    f.clashBalanceDb = 6.25f;
    f.view = { 0, 2 };
    f.cmpA = 2;
    f.cmpB = 3;
    f.clashOn = true;
    f.rangeLo = 40.0f;
    f.rangeHi = 800.0f;
    const auto first = st::write (f, true);
    const auto got = reread (first);
    REQUIRE (got.has_value());
    CHECK (got->fields == f);
    CHECK (got->bypass == std::optional<bool> (true));
    CHECK (st::write (got->fields, *got->bypass) == first);
}

TEST_CASE ("an earlier build's chunk -- the selection only, no header -- still loads")
{
    Bytes legacy;
    putString (legacy, "3,4");
    st::Fields current;
    current.rangeLo = 2000.0f;
    auto got = st::read (legacy.data(), legacy.size(), current);
    REQUIRE (got.has_value());
    CHECK (got->fields.sources == std::vector<unsigned int> { 3, 4 });
    /* What it does not carry keeps the value it had. */
    CHECK (got->fields.rangeLo == 2000.0f);
    CHECK_FALSE (got->bypass.has_value());

    /* ... and with the VST3 wrapper's bypass after it. */
    const std::int32_t on = 1;
    legacy.insert (legacy.end(), reinterpret_cast<const std::uint8_t*> (&on),
                   reinterpret_cast<const std::uint8_t*> (&on) + 4);
    got = st::read (legacy.data(), legacy.size(), st::Fields {});
    REQUIRE (got.has_value());
    CHECK (got->bypass == std::optional<bool> (true));
}

TEST_CASE ("a chunk may end after any string, and each missing one keeps its value")
{
    const auto all = st::strings (variant (3));
    for (std::size_t n = 1; n <= all.size(); ++n)
    {
        CAPTURE (n);
        st::Fields f;
        REQUIRE (st::apply (std::vector<std::string> (all.begin(), all.begin() + (long) n), f));
        st::Fields want;
        want.sources = variant (3).sources;
        if (n > 1)
        {
            want.clashFloorDb = variant (3).clashFloorDb;
            want.clashBalanceDb = variant (3).clashBalanceDb;
        }
        if (n > 2)
            want.view = variant (3).view;
        if (n > 3)
        {
            want.cmpA = variant (3).cmpA;
            want.cmpB = variant (3).cmpB;
            want.clashOn = variant (3).clashOn;
        }
        if (n > 4)
        {
            want.rangeLo = variant (3).rangeLo;
            want.rangeHi = variant (3).rangeHi;
        }
        CHECK (f == want);
    }
}

TEST_CASE ("a string that does not read keeps its value; the selection does not and is refused")
{
    st::Fields f = variant (1);
    CHECK (st::apply ({ "2,7", "nonsense", "", "x", "800:40" }, f));
    CHECK (f.sources == std::vector<unsigned int> { 2, 7 });
    CHECK (f.clashFloorDb == variant (1).clashFloorDb);
    /* A spectrogram showing nothing is a broken plugin, not a view. */
    CHECK (f.view == std::vector<int> { 0 });
    CHECK (f.cmpA == variant (1).cmpA);
    /* A range upside down, or from 0 Hz, is not one. */
    CHECK (f.rangeLo == variant (1).rangeLo);
    CHECK (st::apply ({ "", "", "", "", "0:800" }, f));
    CHECK (f.rangeLo == variant (1).rangeLo);

    const auto before = f;
    CHECK_FALSE (st::apply ({}, f));
    CHECK_FALSE (st::apply ({ "2;5" }, f));
    CHECK (f == before);
}

TEST_CASE ("an empty chunk is refused: the very first build wrote nothing")
{
    CHECK_FALSE (st::read (nullptr, 0, st::Fields {}).has_value());
}

TEST_CASE ("random bytes are refused")
{
    std::mt19937 rng (7);
    for (int round = 0; round < 3; ++round)
    {
        Bytes noise (1024 * 1024);
        for (auto& byte : noise)
            byte = std::uint8_t (rng());
        CHECK_FALSE (reread (noise).has_value());
    }
}

/* -------------------------------------------------------------- session -- */

namespace
{

/* The receiver calls, recorded -- and each one checked to be on the thread
 * that is allowed to make it. `forward` passes them to a real receiver. */
struct Recorder final : st::Session::Sink
{
    std::thread::id main = std::this_thread::get_id();
    srecv_t* forward = nullptr;
    std::atomic<int> offMain { 0 };
    int sources = 0, clash = 0, range = 0;
    st::Fields last;

    void onMain()
    {
        if (std::this_thread::get_id() != main)
            offMain.fetch_add (1);
    }
    void applySources (const std::vector<unsigned int>& slots) override
    {
        onMain();
        sources++;
        last.sources = slots;
        if (forward)
            srecv_set_sources (forward, slots.empty() ? nullptr : slots.data(), int (slots.size()));
    }
    void applyClash (float floorDb, float balanceDb) override
    {
        onMain();
        clash++;
        last.clashFloorDb = floorDb;
        last.clashBalanceDb = balanceDb;
        if (forward)
            srecv_set_clash (forward, floorDb, balanceDb);
    }
    void applyRange (float lo, float hi) override
    {
        onMain();
        range++;
        last.rangeLo = lo;
        last.rangeHi = hi;
        if (forward)
            srecv_set_range (forward, lo, hi);
    }
    int calls() const { return sources + clash + range; }
};

/* A deadlock is the failure these guard against, so it must fail rather than
 * hang: past the bound the process says so and exits. */
struct Watchdog
{
    std::atomic<bool> done { false };
    std::thread t;
    explicit Watchdog (const char* what)
        : t ([this, what] {
              const auto until = std::chrono::steady_clock::now() + std::chrono::seconds (30);
              while (! done.load())
              {
                  if (std::chrono::steady_clock::now() > until)
                  {
                      std::fprintf (stderr, "HUNG: %s\n", what);
                      std::_Exit (1);
                  }
                  std::this_thread::sleep_for (std::chrono::milliseconds (10));
              }
          })
    {
    }
    ~Watchdog()
    {
        done.store (true);
        t.join();
    }
};

bool loadInto (st::Session& s, const Bytes& chunk)
{
    std::optional<bool> bypass;
    return s.load (chunk.data(), chunk.size(), bypass);
}

} // namespace

TEST_CASE ("a load on the host's thread waits for the message thread's next service")
{
    st::Session s;
    Recorder sink;
    const st::Fields loaded = variant (5);
    const Bytes chunk = st::write (loaded, false);
    const auto before = s.revision();

    bool ok = false;
    std::thread host ([&] { ok = loadInto (s, chunk); });
    host.join();
    REQUIRE (ok);
    CHECK (s.revision() != before);

    CHECK (sink.calls() == 0);
    CHECK (s.applied() == st::Fields {});

    /* SAVED STATE NEVER REGRESSES: a save before the message thread has
     * applied the load writes the load, byte for byte. */
    CHECK (st::write (s.get(), false) == chunk);

    /* The next service applies all of it, and says it was a load. */
    CHECK (s.service (sink));
    CHECK (sink.offMain.load() == 0);
    CHECK (sink.sources == 1);
    CHECK (sink.clash == 1);
    CHECK (sink.range == 1);
    CHECK (sink.last.sources == loaded.sources);
    CHECK (sink.last.clashFloorDb == loaded.clashFloorDb);
    CHECK (sink.last.rangeHi == loaded.rangeHi);
    CHECK (s.applied() == loaded);

    /* And nothing twice. */
    CHECK_FALSE (s.service (sink));
    CHECK (sink.calls() == 3);
}

TEST_CASE ("a refused chunk marks nothing and moves no revision")
{
    st::Session s;
    Recorder sink;
    const auto before = s.revision();
    std::thread host ([&] { CHECK_FALSE (loadInto (s, Bytes {})); });
    host.join();
    CHECK (s.revision() == before);
    CHECK_FALSE (s.service (sink));
    CHECK (sink.calls() == 0);
    CHECK (s.get() == st::Fields {});
}

TEST_CASE ("an edit reaches the receiver only in the part that moved")
{
    st::Session s;
    Recorder sink;
    const auto before = s.revision();
    s.edit ([] (st::Fields& f) { f.view = { 0, 1 }; });
    CHECK (s.revision() != before);
    CHECK_FALSE (s.service (sink)); /* an edit is not a load */
    CHECK (sink.calls() == 0);      /* and the view is not the receiver's */
    CHECK (s.applied().view == std::vector<int> { 0, 1 });

    s.edit ([] (st::Fields& f) { f.sources = { 2 }; });
    s.service (sink);
    CHECK (sink.sources == 1);
    CHECK (sink.clash == 0);
    CHECK (sink.range == 0);

    /* A rebuilt receiver is handed everything. */
    s.service (sink, true);
    CHECK (sink.sources == 2);
    CHECK (sink.clash == 1);
    CHECK (sink.range == 1);
}

TEST_CASE ("loads on the host's thread beside the message thread's service: the receiver is "
           "only ever called from the message thread, and the last load is what it ends up with")
{
    Watchdog dog ("state loads beside the message thread's service");
    st::Session s;
    Recorder sink;
    constexpr int loads = 300;
    std::vector<Bytes> chunks;
    for (int i = 0; i < loads; ++i)
        chunks.push_back (st::write (variant (i), false));

    std::atomic<bool> loading { true };
    std::atomic<int> refused { 0 };
    std::thread host ([&] {
        for (const auto& c : chunks)
        {
            if (! loadInto (s, c))
                refused.fetch_add (1);
            /* A save between loads, as auval's stress test does. */
            if (st::write (s.get(), false).empty())
                refused.fetch_add (1);
        }
        loading.store (false);
    });
    while (loading.load())
        s.service (sink);
    host.join();
    s.service (sink);

    CHECK (refused.load() == 0);
    CHECK (sink.offMain.load() == 0);
    CHECK (sink.sources > 0);
    CHECK (s.applied() == variant (loads - 1));
    CHECK (sink.last.sources == variant (loads - 1).sources);
}

TEST_CASE ("the same, against a running receiver: nothing waits forever")
{
    Watchdog dog ("state loads beside a running receiver");
    /* A private set of buses, as tests/srecv_api.c takes: the slots probed
     * here must never be a Live session's. Set before the first bus call. */
    char ns[64];
    std::snprintf (ns, sizeof ns, "spectro_state.%d", int (getpid()));
    setenv ("NIA_BUS_NS", ns, 1);
    srecv_t* r = srecv_new (48000.0f, 8192, 1024, 256, 10.0f, 20000.0f, -96.0f, 0.0f);
    REQUIRE (r);
    REQUIRE (srecv_start (r) == 1);

    st::Session s;
    Recorder sink;
    sink.forward = r;
    constexpr int loads = 100;
    std::vector<Bytes> chunks;
    for (int i = 0; i < loads; ++i)
        chunks.push_back (st::write (variant (i), false));

    std::atomic<bool> loading { true };
    std::thread host ([&] {
        for (const auto& c : chunks)
            loadInto (s, c);
        loading.store (false);
    });
    while (loading.load())
        s.service (sink);
    host.join();
    s.service (sink);

    CHECK (sink.offMain.load() == 0);
    CHECK (s.applied() == variant (loads - 1));
    /* No slot has a sender here, so the receiver draws the own channel alone. */
    CHECK (srecv_channels (r) == 1);
    srecv_free (r);
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Spectrogram's session and its chunk. See State.h.
 */
#include "State.h"

#include "Wire.h"
#include "ni/Wire.h"

#include <functional>

namespace spectro::state
{

namespace
{
/* What the iPlug2 build wrote for the selection: slot numbers and commas. */
bool isSelection (const std::string& s)
{
    for (const char c : s)
        if (! ((c >= '0' && c <= '9') || c == ','))
            return false;
    return true;
}

template <class T>
std::string commaList (const std::vector<T>& values)
{
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0)
            out += ',';
        ni::wire::append_int (out, (long long) values[i]);
    }
    return out;
}

/* Exactly the same value: what a session holds is compared as stored, and a
 * change of any size is a change. */
bool same (float a, float b)
{
    return std::equal_to<float>() (a, b);
}

std::string pair (float a, float b)
{
    std::string out;
    ni::wire::append_fixed (out, a, 2);
    out += ':';
    ni::wire::append_fixed (out, b, 2);
    return out;
}
} // namespace

bool Fields::operator== (const Fields& o) const
{
    return sources == o.sources && same (clashFloorDb, o.clashFloorDb) && same (clashBalanceDb, o.clashBalanceDb)
        && view == o.view && cmpA == o.cmpA && cmpB == o.cmpB && clashOn == o.clashOn
        && same (rangeLo, o.rangeLo) && same (rangeHi, o.rangeHi);
}

const ni::nist::Layout& layout()
{
    /* The selection, then the clash, the view, the comparison and the zoom,
     * each added after the one before it. */
    static const ni::nist::Layout l { nullptr, 0, {}, 1, 5 };
    return l;
}

std::vector<std::string> strings (const Fields& f)
{
    std::string compare;
    ni::wire::append_int (compare, f.cmpA);
    compare += ':';
    ni::wire::append_int (compare, f.cmpB);
    compare += f.clashOn ? ":1" : ":0";
    return { commaList (f.sources), pair (f.clashFloorDb, f.clashBalanceDb), commaList (f.view), compare,
             pair (f.rangeLo, f.rangeHi) };
}

/*
 * READ BACK DEFENSIVELY, BUT NOT BLINDLY. The chunk grew a string at a time,
 * so one saved by an earlier build ends early, and every string after the
 * selection is taken only if it is there and reads. The selection is not
 * optional: see State.h.
 */
bool apply (const std::vector<std::string>& s, Fields& f)
{
    if (s.empty() || ! isSelection (s[0]))
        return false;

    Fields next = f;
    next.sources.clear();
    spectro::wire::parse_slots (s[0], next.sources);

    float a = 0.0f, b = 0.0f;
    if (s.size() > 1 && spectro::wire::parse_range (s[1], a, b))
    {
        next.clashFloorDb = a;
        next.clashBalanceDb = b;
    }

    if (s.size() > 2)
    {
        std::vector<int> view;
        spectro::wire::parse_channels (s[2], view);
        /* A spectrogram showing nothing is a broken plugin, not a view. */
        next.view = view.empty() ? std::vector<int> { 0 } : std::move (view);
    }

    int ca = 0, cb = 0;
    bool on = false;
    if (s.size() > 3 && spectro::wire::parse_compare (s[3], ca, cb, on))
    {
        next.cmpA = ca;
        next.cmpB = cb;
        next.clashOn = on;
    }

    if (s.size() > 4 && spectro::wire::parse_range (s[4], a, b) && a > 0.0f && b > a)
    {
        next.rangeLo = a;
        next.rangeHi = b;
    }

    f = std::move (next);
    return true;
}

std::vector<std::uint8_t> write (const Fields& f, bool bypass)
{
    ni::nist::State s;
    s.strings = strings (f);
    s.bypass = bypass;
    return ni::nist::write (layout(), s);
}

std::optional<Loaded> read (const void* data, std::size_t size, const Fields& current)
{
    const auto state = ni::nist::read (layout(), data, size);
    if (! state)
        return std::nullopt;
    Loaded out { current, state->bypass };
    if (! apply (state->strings, out.fields))
        return std::nullopt;
    return out;
}

/* --------------------------------------------------------------- Session */

Session::Session (const Fields& initial) : wanted (initial), done (initial) {}

Fields Session::get() const
{
    const std::lock_guard<std::mutex> hold (lock);
    return wanted;
}

/* Held across the parse: what the stream does not carry keeps the value it
 * had at this moment, and an edit cannot land between the copy and the
 * result. */
bool Session::load (const void* data, std::size_t size, std::optional<bool>& bypass)
{
    const std::lock_guard<std::mutex> hold (lock);
    auto got = read (data, size, wanted);
    if (! got)
        return false;
    wanted = std::move (got->fields);
    bypass = got->bypass;
    changed = true;
    loaded = true;
    rev.fetch_add (1, std::memory_order_release);
    return true;
}

void Session::edit (const std::function<void (Fields&)>& change)
{
    const std::lock_guard<std::mutex> hold (lock);
    change (wanted);
    changed = true;
    rev.fetch_add (1, std::memory_order_release);
}

bool Session::service (Sink& sink, bool all)
{
    Fields next;
    bool wasLoad = false;
    {
        const std::lock_guard<std::mutex> hold (lock);
        if (! changed && ! all)
            return false;
        next = wanted;
        wasLoad = loaded;
        changed = false;
        loaded = false;
    }
    /* Only what moved: choosing sources waits for the analysis thread, and a
     * view or a comparison is not the receiver's business at all. */
    if (all || next.sources != done.sources)
        sink.applySources (next.sources);
    if (all || ! same (next.clashFloorDb, done.clashFloorDb) || ! same (next.clashBalanceDb, done.clashBalanceDb))
        sink.applyClash (next.clashFloorDb, next.clashBalanceDb);
    if (all || ! same (next.rangeLo, done.rangeLo) || ! same (next.rangeHi, done.rangeHi))
        sink.applyRange (next.rangeLo, next.rangeHi);
    done = std::move (next);
    return wasLoad;
}

} // namespace spectro::state

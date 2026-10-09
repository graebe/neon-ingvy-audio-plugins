// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The kick behind the duck. Kick.h says what it is and on which thread.
 */
#include "Kick.h"

#include "ni/Wire.h"

#include <algorithm>

namespace ni::sc
{

namespace
{
/* The handoff frees through this; abus_reader_close's pointer type is not
 * void*. */
void closeReader (void* r)
{
    abus_reader_close (static_cast<abus_reader_t*> (r));
}

/* How often a bus is looked at again: one that is not there yet (a
 * Listen-In inserted later), or one whose sender quit and came back on a
 * fresh segment, which a reader of the old one never hears. */
constexpr std::uint32_t retryMs = 500;
} // namespace

namespace kick
{
std::string toText (int choice)
{
    if (choice == key)
        return "key";
    if (choice > 0)
    {
        std::string out = "bus:";
        ni::wire::append_int (out, choice);
        return out;
    }
    return {};
}

int fromText (const std::string& text)
{
    if (text == "key")
        return key;
    int slot = 0;
    if (text.rfind ("bus:", 0) == 0 && ni::wire::parse_int (text.substr (4), slot) && slot >= 1
        && slot <= ABUS_MAX_SLOT)
        return slot;
    return off;
}
} // namespace kick

Kick::Kick() : tap (sc_kick_create()), handoff (shell_handoff_new (closeReader)) {}

/* No block runs any more, so the lent reader goes too. */
Kick::~Kick()
{
    shell_handoff_free (handoff);
}

bool Kick::choose (int choice)
{
    if (choice != kick::key && (choice < kick::off || choice > ABUS_MAX_SLOT))
        choice = kick::off;
    if (wanted.exchange (choice, std::memory_order_relaxed) == choice)
        return false;
    band.Retire();
    return true;
}

void Kick::service (bool fresh)
{
    shell_handoff_collect (handoff);
    const int want = std::max (0, wanted.load (std::memory_order_relaxed));
    const auto now = juce::Time::getMillisecondCounter();

    /* Now and then: a missing bus may have been inserted since, and one the
     * reader maps may have been replaced under its name. A bus that is merely
     * quiet -- its Listen-In bypassed -- keeps its reader, and says silent. */
    bool stale = false;
    if (want > 0 && want == openSlot && now - lastTry >= retryMs)
    {
        lastTry = now;
        const auto current = abus_incarnation ((std::uint32_t) want);
        stale = ! hasReader ? current != 0 : current != 0 && current != openedAs;
    }
    if (want == openSlot && ! fresh && ! stale)
        return;

    abus_reader_t* r = nullptr;
    if (want > 0)
    {
        lastTry = now;
        if (abus_reader_open ((std::uint32_t) want, &r) != ABUS_OK)
            r = nullptr;
    }
    /* Asked before it is lent: from here on the audio thread reads it. */
    openedAs = abus_reader_incarnation (r);
    shell_handoff_set (handoff, r);
    openSlot = want;
    hasReader = r != nullptr;
}

int Kick::status (bool keyConnected) const
{
    const int c = wanted.load (std::memory_order_relaxed);
    if (c == kick::off)
        return SC_KICK_OFF;
    if (c == kick::key)
        return keyConnected ? SC_KICK_ALIGNED : SC_KICK_WAITING;
    if (! hasReader)
        return SC_KICK_WAITING;
    const int s = sc_kick_status (tap.get());
    return s == SC_KICK_OFF ? SC_KICK_WAITING : s;
}

void Kick::prepare (std::uint32_t sampleRate, int maximumBlock)
{
    sc_kick_prepare (tap.get(), sampleRate);
    const auto n = (std::size_t) std::max (1, maximumBlock);
    x.assign (n, 0.0f);
    at.assign (n, 0.0f);
    band.Clear();
}

void Kick::file (const float* sweep, int frames, bool timed, std::int64_t timeline, const float* keyL,
                 const float* keyR)
{
    const int c = wanted.load (std::memory_order_relaxed);
    const int cap = (int) x.size();
    if (c == kick::off || frames <= 0 || cap <= 0)
        return;

    if (c == kick::key)
    {
        /* The key is this block's own: its sweep is the engine's, sample for
         * sample. */
        if (keyL == nullptr)
            return;
        for (int done = 0; done < frames; done += cap)
        {
            const int n = std::min (cap, frames - done);
            for (int i = 0; i < n; ++i)
                x[(std::size_t) i] = 0.5f * (keyL[done + i] + keyR[done + i]);
            band.Push (x.data(), sweep + done, n);
        }
        return;
    }

    sc_kick_note (tap.get(), timeline, timed ? 1 : 0, sweep, frames);
    auto* reader = static_cast<abus_reader_t*> (shell_handoff_acquire (handoff));
    for (;;)
    {
        const int n = sc_kick_drain (tap.get(), reader, x.data(), at.data(), cap);
        if (n > 0)
            band.Push (x.data(), at.data(), n);
        if (n < cap)
            break;
    }
    shell_handoff_release (handoff);
}

} // namespace ni::sc

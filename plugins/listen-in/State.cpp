// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In's parameter, state layout and name session. State.h says what
 * they must stay.
 */
#include "State.h"

#include "audio_bus.h"

#include <utility>

namespace ni::li
{

namespace
{
/* The iPlug2 build's InitInt ("Bus", 1, 1, abus_max_slot()): the engine's
 * own limit, as a macro the generated header carries. */
const ParamSpec table[kNumParams] {
    { "bus", "Bus", ParamSpec::Kind::integer, 1, ABUS_MAX_SLOT, 1, "" },
};
} // namespace

const ParamSpec& busSpec()
{
    return table[kBus];
}

const nist::Layout& layout()
{
    static const nist::Layout l { table, kNumParams, { kNumParams }, 1, 1 };
    return l;
}

std::string Session::label() const
{
    const std::lock_guard<std::mutex> hold (lock);
    return name;
}

void Session::load (std::string label)
{
    const std::lock_guard<std::mutex> hold (lock);
    name = std::move (label);
    changed = true;
    loaded = true;
}

void Session::edit (std::string label)
{
    const std::lock_guard<std::mutex> hold (lock);
    name = std::move (label);
    changed = true;
}

bool Session::take (std::string& label, bool& wasLoaded)
{
    const std::lock_guard<std::mutex> hold (lock);
    wasLoaded = loaded;
    if (! changed)
        return false;
    /* Copied under the lock; the caller hands it to the bus after. */
    label = name;
    changed = false;
    loaded = false;
    return true;
}

} // namespace ni::li

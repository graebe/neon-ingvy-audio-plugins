// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Detents: values a control holds on while it is dragged past them -- the web
 * kit's lib/detents.js, for JUCE, and the Knob card's 1.1.0 rule.
 *
 * A Length knob at 1/32 has 128 values on 200px of travel, a step and a half
 * of a pixel each, and the four a user is reaching for -- 16, 32, 64, 128 --
 * are as hard to land on as the other 124. A detent makes them easy without
 * making them the only ones.
 *
 * HOW: EVERY DETENT IS GIVEN A STRETCH OF TRAVEL OF ITS OWN. The drag is
 * measured along a "travel" axis that is the value axis with a flat run of
 * holdPx inserted at each detent; over that run the value IS the detent. A
 * drag passing through holds there for the same distance from either side, and
 * nowhere else is the knob any slower than it was. A drag that starts on a
 * detent starts in the middle of its run: half the hold to leave it, either
 * way.
 *
 * A pure function of where the press was and how far the pointer has moved
 * since -- no state, no timers -- so a drag back over the same pixels gives
 * the same values. Shift (fine) ignores the detents: a fine drag is for the
 * values between them. Page Up and Page Down jump to the next one (next()).
 *
 * WHAT IT IS NOT: the value. Detents are a feel in the editor only; the
 * parameter, its automation and its host stay exactly as continuous or as
 * stepped as they were. Values are in whatever unit the caller uses --
 * normalised 0..1 for a knob, a count for a ring -- as long as the detents
 * are in the same one.
 */
#pragma once

#include <optional>
#include <vector>

namespace ni::ui::detents
{

/*
 * 14px OF TRAVEL PER DETENT. Under 12 a drag at a normal speed crosses it in
 * a single pointer event and it is not felt; over 16 it is felt as a catch,
 * and at 128 values on 200px it would be the width of ten values.
 */
inline constexpr double holdPx = 14.0;

/* The detents finite, ascending, without duplicates and inside [lo, hi]. */
std::vector<double> clean (const std::vector<double>& detents, double lo = 0.0, double hi = 1.0);

/* Where `value` sits on the travel axis: past every detent below it by a
 * full hold, and on a detent in the middle of its hold. `hold` is in value
 * units (holdPx / the travel's px). */
double travel (double value, const std::vector<double>& detents, double hold);

/* The value at `travel`: the inverse, flat across each detent's hold.
 * Unclamped; the caller clamps. */
double valueAt (double travel, const std::vector<double>& detents, double hold);

/*
 * A drag's value, normalised: `start` where the press was, `dy` pixels moved
 * since (up is positive), `travelPx` the pixels for the whole range (200, or
 * 1000 with Shift). `fine` ignores the detents. Clamped to 0..1.
 */
double dragValue (double start, double dy, double travelPx,
                  const std::vector<double>& detents = {}, bool fine = false);

/* The nearest detent above `value` (dir > 0) or below it, or nothing when
 * there is none that way. Any units. */
std::optional<double> next (double value, const std::vector<double>& detents, int dir);

} // namespace ni::ui::detents

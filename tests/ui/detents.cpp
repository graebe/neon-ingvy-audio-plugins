// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Detents: the values a control holds on while it is dragged past them. The
 * web kit's detents.test.mjs, case for case, against the native functions --
 * the Length knob at 1/32 in 4/4, holding at 16, 32, 64 and 128 steps.
 */
#include "Detents.h"

#include <doctest.h>

#include <cmath>
#include <limits>

using namespace ni::ui::detents;

namespace
{
double norm (int steps) { return (steps - 1) / 127.0; }
int steps (double v) { return (int) std::lround (v * 127.0) + 1; }

const std::vector<double> D { norm (16), norm (32), norm (64), norm (128) };
constexpr double TRAVEL = 200.0;
const double HOLD = holdPx / TRAVEL;

/* Where a drag that started at `from` steps is after `dy` px upwards. */
int drag (int from, double dy, double travelPx = TRAVEL, bool fine = false)
{
    return steps (dragValue (norm (from), dy, travelPx, D, fine));
}
} // namespace

TEST_CASE ("detents: the hold is a fixed distance, chosen to land on but never stick")
{
    CHECK (holdPx >= 12.0);
    CHECK (holdPx <= 16.0);
}

TEST_CASE ("detents: without detents the mapping is the identity")
{
    for (const double v : { 0.0, 0.25, 0.5, 1.0 })
    {
        CHECK (travel (v, {}, HOLD) == v);
        CHECK (valueAt (v, {}, HOLD) == v);
    }
    CHECK (dragValue (0.5, 20.0, 200.0) == doctest::Approx (0.6));
}

TEST_CASE ("detents: a drag passing near a detent lands on it and holds there")
{
    const double reach = (32 - 20) / 127.0 * TRAVEL;
    CHECK (drag (20, reach - 1.6) == 31);                  // one step short is not yet on it
    for (double px = 0.0; px <= holdPx; px += 0.5)
        CHECK (drag (20, reach + px) == 32);
    CHECK (drag (20, reach + holdPx + 1.6) == 33);         // and then it moves on
}

TEST_CASE ("detents: the hold is the same distance coming down")
{
    const double reach = (40 - 32) / 127.0 * TRAVEL;
    CHECK (drag (40, -(reach - 1.6)) == 33);
    for (double px = 0.0; px <= holdPx; px += 0.5)
        CHECK (drag (40, -(reach + px)) == 32);
    CHECK (drag (40, -(reach + holdPx + 1.6)) == 31);
}

TEST_CASE ("detents: a drag that starts on a detent leaves it after half the hold, either way")
{
    const double half = holdPx / 2.0;
    CHECK (drag (32, half - 0.5) == 32);
    CHECK (drag (32, -(half - 0.5)) == 32);
    CHECK (drag (32, half + 1.6) == 33);
    CHECK (drag (32, -(half + 1.6)) == 31);
    CHECK (dragValue (norm (32), 0.0, TRAVEL, D) == doctest::Approx (norm (32)));  // a press is no change
}

TEST_CASE ("detents: the mapping is monotonic and reaches both ends")
{
    double last = -1.0;
    for (double u = 0.0; u <= 1.0 + HOLD * (double) D.size(); u += 0.001)
    {
        const double v = valueAt (u, D, HOLD);
        CHECK (v >= last - 1.0e-12);
        last = v;
    }
    CHECK (valueAt (0.0, D, HOLD) == 0.0);
    CHECK (valueAt (1.0 + HOLD * (double) D.size(), D, HOLD) == doctest::Approx (1.0));
    CHECK (drag (1, 1000.0) == 128);
    CHECK (drag (128, -1000.0) == 1);
}

TEST_CASE ("detents: travel and value are inverses off the detents, and a detent is its hold's middle")
{
    for (const double v : { 0.01, 0.2, 0.4, 0.9 })
        CHECK (std::abs (valueAt (travel (v, D, HOLD), D, HOLD) - v) < 1.0e-12);

    const double u = travel (D[1], D, HOLD);
    CHECK (valueAt (u - HOLD / 2.0 + 1.0e-9, D, HOLD) == D[1]);
    CHECK (valueAt (u + HOLD / 2.0 - 1.0e-9, D, HOLD) == D[1]);
}

TEST_CASE ("detents: a fine drag ignores the detents")
{
    const double fine = TRAVEL * 5.0;
    const double reach = (32 - 31) / 127.0 * fine;
    CHECK (drag (31, reach, fine, true) == 32);
    CHECK (drag (31, reach + 8.0, fine, true) == 33);
}

TEST_CASE ("detents: detents out of range or unsorted are tolerated")
{
    const std::vector<double> messy { norm (64), 2.0, norm (16), -1.0,
                                      std::numeric_limits<double>::quiet_NaN(), norm (32), norm (16) };
    CHECK (steps (valueAt (travel (norm (32), messy, HOLD), messy, HOLD)) == 32);
    CHECK (*next (norm (20), messy, 1) == doctest::Approx (norm (32)));
}

TEST_CASE ("detents: Page Up and Down go to the next detent, and past the last there is none")
{
    CHECK (*next (norm (20), D, 1) == doctest::Approx (norm (32)));
    CHECK (*next (norm (20), D, -1) == doctest::Approx (norm (16)));
    CHECK (*next (norm (32), D, 1) == doctest::Approx (norm (64)));   // from on one, the next
    CHECK (*next (norm (32), D, -1) == doctest::Approx (norm (16)));
    CHECK_FALSE (next (norm (128), D, 1).has_value());
    CHECK_FALSE (next (norm (10), D, -1).has_value());
    CHECK_FALSE (next (0.5, {}, 1).has_value());
}

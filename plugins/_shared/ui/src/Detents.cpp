// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Detents. Detents.h has the feel and its numbers.
 */
#include "Detents.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ni::ui::detents
{

namespace
{
/* Two values this close are the same detent: well under half of the finest
 * step any control here has (1/127 of the range). */
constexpr double eps = 1.0e-4;
} // namespace

std::vector<double> clean (const std::vector<double>& detents, double lo, double hi)
{
    std::vector<double> sorted;
    for (const double d : detents)
        if (std::isfinite (d) && d >= lo && d <= hi)
            sorted.push_back (d);
    std::sort (sorted.begin(), sorted.end());

    std::vector<double> out;
    for (const double d : sorted)
        if (out.empty() || std::abs (d - out.back()) > eps)
            out.push_back (d);
    return out;
}

double travel (double value, const std::vector<double>& detents, double hold)
{
    double off = 0.0;
    for (const double d : clean (detents))
    {
        if (std::abs (value - d) <= eps)
            return d + off + hold / 2.0;
        if (value < d)
            break;
        off += hold;
    }
    return value + off;
}

double valueAt (double t, const std::vector<double>& detents, double hold)
{
    double off = 0.0;
    for (const double d : clean (detents))
    {
        const double start = d + off;
        if (t < start)
            return t - off;
        if (t <= start + hold)
            return d;
        off += hold;
    }
    return t - off;
}

double dragValue (double start, double dy, double travelPx, const std::vector<double>& detents, bool fine)
{
    const auto clamp = [] (double v) { return std::min (1.0, std::max (0.0, v)); };
    const auto ds = clean (detents);
    if (fine || ds.empty())
        return clamp (start + dy / travelPx);

    const double hold = holdPx / travelPx;
    return clamp (valueAt (travel (start, ds, hold) + dy / travelPx, ds, hold));
}

std::optional<double> next (double value, const std::vector<double>& detents, int dir)
{
    const auto inf = std::numeric_limits<double>::infinity();
    const auto ds = clean (detents, -inf, inf);

    if (dir > 0)
    {
        for (const double d : ds)
            if (d > value + eps)
                return d;
        return std::nullopt;
    }

    for (auto it = ds.rbegin(); it != ds.rend(); ++it)
        if (*it < value - eps)
            return *it;
    return std::nullopt;
}

} // namespace ni::ui::detents

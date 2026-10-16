// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What NI Filter Designer's editor needs from its plugin, and all it may ask.
 *
 * EVERY CONTROL IS A HOST PARAMETER, the engine's 175 in its table's order
 * (fd_param_*): parameter(param::node (2, param::freq)) is the third node's
 * Freq. Their text is the engine's ("1.20 kHz", "-inf dB", "Notch"), and the
 * editor holds no unit, precision or range of its own. Which node is
 * selected, and the dB range of the picture, are the window's own and saved
 * nowhere.
 *
 * WHAT IT SHOWS, ALL OF IT THE ENGINE'S:
 *
 *   picture()   every node's curve, the total in a lane, the isolator's
 *               bands, the analyzer -- designed from the values the host
 *               holds NOW with the engine's own function (fd_shell_read), so
 *               a handle dragged moves its curve in the same frame
 *   uses()      what a type uses at an order (Family, Order, Gain, Q) and
 *               what a family uses (Ripple, Stopband): which controls are lit
 *
 * MESSAGE THREAD, EVERY CALL (EditorModel.h). The processor implements this
 * (FilterDesigner.h); tests/ui/filter-designer_fakes.h implements it with the
 * engine's pure functions and no audio behind it.
 */
#pragma once

#include "EditorModel.h"

#include <array>
#include <cstdint>

namespace ni::fd
{

/* The host parameters' indices: the engine table's order. */
namespace param
{
enum : int
{
    output = 0,   // -24..+24 dB
    scale,        // -200..+200 %, every gain at once
    adaptiveQ,    // switch: a Bell narrows as it boosts or cuts
    analyzer,     // Off, Pre, Post, Both; a view, not automated
    isoOn,        // switch
    isoLow,       // -60 (-inf)..+6 dB
    isoMid,
    isoHigh,
    isoKillLow,   // switches
    isoKillMid,
    isoKillHigh,
    isoLower,     // 30..2000 Hz, the low-mid crossover frequency
    isoUpper,     // 500..16000 Hz, the mid-high crossover frequency
    isoSlope,     // LR4 24 dB/oct, LR8 48 dB/oct
    nodeBase,
    /* Appended after the nodes. */
    matchedCuts = nodeBase + 16 * 10, // switch: high-pass and low-pass matched to the analog magnitude
    count
};

/* A node's fields, in their order. */
enum Field : int
{
    type = 0,   // Empty, Bell, Low Shelf, High Shelf, High-Pass, Low-Pass, Band-Pass, Notch, All-Pass, Tilt
    on,         // switch: the bypass
    family,     // Butterworth, Bessel, Chebyshev I, Chebyshev II, Elliptic, Linkwitz-Riley
    order,      // 0..7: 6 dB/oct .. 48 dB/oct
    freq,       // 10..22000 Hz
    gain,       // -30..+30 dB
    q,          // 0.1..40
    ripple,     // 0.01..3 dB
    stopband,   // 20..120 dB
    channel,    // Stereo, Left, Right, Mid, Side
    fields
};

inline constexpr int nodes = 16;

/* Node `slot` (0..15)'s `field`. */
constexpr int node (int slot, int field) noexcept { return nodeBase + fields * slot + field; }
} // namespace param

/* The types, by their Type value. */
namespace type
{
enum : int
{
    empty = 0,
    bell,
    lowShelf,
    highShelf,
    highPass,
    lowPass,
    bandPass,
    notch,
    allPass,
    tilt,
    count
};
} // namespace type

/* The channels a node can filter, and the lane a total is drawn in. */
namespace lane
{
enum : int
{
    left = 0,   // a stereo signal's left (and right)
    right,
    mid,
    side
};
} // namespace lane

/*
 * THE PICTURE: at each of `hz` (lowest first, equally spaced in pitch), in
 * dB. `node` is each node alone, a bypassed one as it would sound and an
 * Empty one flat; `total` everything heard in the lane asked for; `iso` the
 * isolator's three bands at their levels and `isoTotal` their sum. The
 * analyzer is `pre` and `post` in dBFS, live while `analyzer`. `clamped`
 * says the sample rate narrows a node's band.
 */
struct Picture
{
    static constexpr int points = 512;
    using Curve = std::array<float, points>;

    Curve hz {};
    std::array<Curve, param::nodes> node {};
    std::array<bool, param::nodes> clamped {};
    /* Where each node's handle sits, Hz and dB (NaN for an Empty one), and
     * a Bell's, band-pass's or notch's -3 dB edges (NaN for others): the
     * engine's (fd_core::design::handle, edges). */
    std::array<double, param::nodes> handleHz {}, handleDb {};
    std::array<std::array<double, 2>, param::nodes> edgeHz {};
    Curve total {};
    std::array<Curve, 3> iso {};
    Curve isoTotal {};
    bool analyzer = false;
    Curve pre {}, post {};
    double sampleRate = 0.0;
    std::uint32_t serial = 0;
};

/* What a type uses, as bits (fd_type_uses). */
namespace uses
{
enum : unsigned
{
    family = 1,
    order = 2,
    gain = 4,
    q = 8
};
} // namespace uses

class Model : public ni::ui::EditorModel
{
public:
    /* The shell's Bypass (ni::bypassSpec): the host's own bypass, which the
     * window shows as a switch. Not one of the engine's 175. */
    virtual juce::RangedAudioParameter& bypassParameter() = 0;

    /* Whether parameter `index` runs both ways about zero: the table says. */
    virtual bool bipolar (int index) const = 0;

    /* The picture as the host's values have it now, the total in `lane`;
     * valid until the next call. */
    virtual const Picture& picture (int lane) = 0;

    /* What type `type` uses at order index `order` (uses::*). */
    virtual unsigned typeUses (int type, int order) const = 0;
    /* The order index a node of `type` in `family` at order index `order`
     * plays: at most the type's highest, and a Linkwitz-Riley high-pass or
     * low-pass at an even order. What a choice of Order or Family snaps to. */
    virtual int orderPlayed (int type, int family, int order) const = 0;
    /* Whether `family` takes Ripple, and Stopband. */
    virtual bool familyUsesRipple (int family) const = 0;
    virtual bool familyUsesStopband (int family) const = 0;
    /* The Q whose -3 dB edge sits at `edgeHz` for a centre at `freq`. */
    virtual double qForEdge (double freq, double edgeHz) const = 0;
    /* The type a filter added at `hz` is. */
    virtual int typeForNew (double hz) const = 0;
};

} // namespace ni::fd

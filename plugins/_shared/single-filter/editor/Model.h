// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What a single filter's editor needs from its plugin, and all it may ask --
 * one interface for the nine products (NI High Pass ... NI All Pass).
 *
 * EVERY CONTROL IS A HOST PARAMETER, the product's own few in its table's
 * order (sf_param_*); which field sits where is the engine's to say
 * (indexOf: -1 for a field the product lacks), so the editor holds no
 * layout of its own. Their text is the engine's ("1.20 kHz", "Elliptic").
 * The picture's dB range is the window's own and saved nowhere.
 *
 * WHAT IT SHOWS, ALL OF IT THE ENGINE'S:
 *
 *   picture()   the filter's curve in its lane, with Output, its phase, the
 *               handle and edges, the analyzer of that lane -- designed from
 *               the values the host holds NOW with the engine's own function
 *               (sf_shell_read), so a handle dragged moves its curve in the
 *               same frame
 *   uses()      what the product uses at an order and a family (Q, Ripple,
 *               Stopband): which controls are lit
 *
 * MESSAGE THREAD, EVERY CALL (EditorModel.h). The processor implements this
 * (SingleFilter.h); tests/ui/single-filter_fakes.h implements it with the
 * engine's pure functions and no audio behind it.
 */
#pragma once

#include "EditorModel.h"

#include <array>
#include <cstdint>

namespace ni::sf
{

/* A single filter's fields (sf_core.h SF_FIELD_*): where each sits is the
 * product's (Model::indexOf). */
namespace field
{
enum : int
{
    freq = 0,   // 10..22000 Hz
    gain,       // -30..+30 dB
    q,          // 0.1..40
    adaptiveQ,  // switch: a Bell narrows as it boosts or cuts
    order,      // the slope, 6 dB/oct a step (an All Pass's order)
    family,     // Butterworth, Bessel, Chebyshev I, Chebyshev II, Elliptic, Linkwitz-Riley
    ripple,     // 0.01..3 dB
    stopband,   // 20..120 dB
    matched,    // switch: a cut matched to the analog magnitude
    channel,    // Stereo, Left, Right, Mid, Side
    output,     // -24..+24 dB
    analyzer,   // Off, Pre, Post, Both; a view, not automated
    count
};
} // namespace field

/* The products, by their filter's shape (sf_core.h SF_SHAPE_*). */
namespace shape
{
enum : int
{
    bell = 1,
    lowShelf,
    highShelf,
    highPass,
    lowPass,
    bandPass,
    notch,
    allPass,
    tilt
};
} // namespace shape

/*
 * THE PICTURE: at each of `hz` (lowest first, equally spaced in pitch).
 * `curve` is the filter alone in dB, `total` with Output (what its lane
 * hears), `phase` its phase in degrees, wrapped to ±180 -- what an All Pass
 * is drawn by. The handle sits at (handleHz, handleDb) on the filter alone;
 * a Bell's, band-pass's or notch's -3 dB edges are `edgeHz` (NaN for
 * others). The analyzer is `pre` and `post` in dBFS, of the filter's lane,
 * live while `analyzer`. `clamped` says the sample rate narrows its band.
 */
struct Picture
{
    static constexpr int points = 512;
    using Curve = std::array<float, points>;

    Curve hz {}, curve {}, total {}, phase {};
    bool clamped = false;
    double handleHz = 0.0, handleDb = 0.0;
    std::array<double, 2> edgeHz {};
    bool analyzer = false;
    Curve pre {}, post {};
    double sampleRate = 0.0;
    std::uint32_t serial = 0;
};

/* What the product uses now, as bits (sf_uses). */
namespace uses
{
enum : unsigned
{
    q = 1,
    ripple = 2,
    stopband = 4
};
} // namespace uses

class Model : public ni::ui::EditorModel
{
public:
    /* The product's shape (shape::*), and its name: "High Pass". */
    virtual int shape() const = 0;
    virtual juce::String productName() const = 0;

    /* Where the product declares `field` (field::*); -1 when it has none. */
    virtual int indexOf (int field) const = 0;

    /* The shell's Bypass (ni::bypassSpec): the host's own bypass, which the
     * window shows as a switch. Not one of the engine's parameters. */
    virtual juce::RangedAudioParameter& bypassParameter() = 0;

    /* Whether parameter `index` runs both ways about zero: the table says. */
    virtual bool bipolar (int index) const = 0;

    /* The picture as the host's values have it now; valid until the next
     * call. */
    virtual const Picture& picture() = 0;

    /* What the product uses at order index `order` in family index
     * `family` (uses::*). */
    virtual unsigned uses (int order, int family) const = 0;
    /* The order index the product in `family` plays at order index
     * `order`: a Linkwitz-Riley cut's is even. What a choice snaps to. */
    virtual int orderPlayed (int family, int order) const = 0;
    /* The Q whose -3 dB edge sits at `edgeHz` for a centre at `freq`. */
    virtual double qForEdge (double freq, double edgeHz) const = 0;
};

} // namespace ni::sf

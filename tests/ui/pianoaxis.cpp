// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The PianoAxis: its rows exactly the PitchView's at every zoom and scroll,
 * the keys' shapes, the ends of MIDI, what it paints for a lit key, a dimmed
 * one and a scale's keys and tonic, and that it takes nothing. Then its
 * gallery page, as a picture. (The Keyboard, which paints its keys with the
 * same KeyPaint, keeps its own picture in music.cpp.)
 */
#include "Gallery.h"
#include "KeyPaint.h"
#include "PianoAxis.h"
#include "PitchView.h"
#include "UvTokens.h"
#include "checks.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::PianoAxis;
using ni::ui::PitchView;
namespace c = uv::tok::colour;

namespace
{
ni::ui::zoom::Axis view (double from, double to)
{
    ni::ui::zoom::Axis a;
    a.lo = 0.0;
    a.hi = 127.0;
    a.minSpan = 6.0;
    a.show (from, to);
    return a;
}

/* 18 semitones on 360px: every row 20px, its edges on whole pixels. */
struct Axis20
{
    Axis20()
    {
        keys.setBounds (0, 0, PianoAxis::defaultWidth, 360);
        keys.setAxis (view (55.5, 73.5));
    }
    PianoAxis keys;
};

constexpr std::uint16_t cMajor = 0b1010'1011'0101;

/* Whether two colours are the same within the snapshots' tolerance. */
bool near (juce::Colour a, juce::Colour b)
{
    const auto d = [] (juce::uint8 x, juce::uint8 y) { return std::abs ((int) x - (int) y); };
    return d (a.getRed(), b.getRed()) <= 4 && d (a.getGreen(), b.getGreen()) <= 4 && d (a.getBlue(), b.getBlue()) <= 4;
}
} // namespace

TEST_CASE ("piano axis: its rows are the pitch view's, at every zoom and scroll")
{
    PianoAxis keys;
    PitchView rows;
    for (const auto& a : { view (55.5, 73.5), view (0.0, 127.0), view (-20.0, 10.0), view (120.0, 140.0),
                           view (60.0, 66.0), view (30.25, 91.75) })
    {
        CAPTURE (a.start);
        CAPTURE (a.span);
        for (const int height : { 120, 360, 701 })
        {
            keys.setBounds (0, 0, 32, height);
            rows.setBounds (0, 0, 240, height);
            keys.setAxis (a);
            rows.setAxis (a);
            CHECK (keys.getAxis().start == doctest::Approx (a.start));
            for (int m = 0; m < 128; m += 7)
            {
                CHECK (keys.yOf (m) == doctest::Approx (rows.yOf (m)));
                CHECK (keys.rowBounds (m).getY() == doctest::Approx (rows.yOf (m + 0.5)));
                CHECK (keys.rowBounds (m).getBottom() == doctest::Approx (rows.yOf (m - 0.5)));
            }
            for (const float y : { 0.0f, 33.3f, (float) height })
                CHECK (keys.pitchAt (y) == doctest::Approx (rows.pitchAt (y)));
        }
    }

    /* The view's extremes: its margin past MIDI's ends, at either end. */
    auto low = view (0.0, 24.0);
    low.pan (-1000.0);
    auto high = view (100.0, 127.0);
    high.pan (1000.0);
    keys.setBounds (0, 0, 32, 300);
    keys.setAxis (low);
    CHECK (keys.yOf (low.start) == doctest::Approx (300.0));
    CHECK (keys.rowBounds (0).getBottom() < 300.0f);   // C-2 is on screen, the margin under it
    keys.setAxis (high);
    CHECK (keys.yOf (high.end()) == doctest::Approx (0.0));
    CHECK (keys.rowBounds (127).getY() > 0.0f);
}

TEST_CASE ("piano axis: a black key is its row from the back, a white one takes half of each black row by it")
{
    Axis20 a;
    const auto& k = a.keys;
    /* C3 (60): its row from y 260 to 280; C#'s from 240. */
    CHECK (k.rowBounds (60) == juce::Rectangle<float> (0.0f, 260.0f, 32.0f, 20.0f));

    const auto cSharp = k.keyBounds (61);
    CHECK (cSharp == juce::Rectangle<float> (0.0f, 240.0f, 32.0f * ni::ui::keypaint::blackShare, 20.0f));

    /* C: up to the middle of C#, down to B's edge; the 1px gap at its top. */
    CHECK (k.keyBounds (60) == juce::Rectangle<float> (0.0f, 251.0f, 32.0f, 29.0f));
    /* D: half of C# and half of D#. */
    CHECK (k.keyBounds (62) == juce::Rectangle<float> (0.0f, 211.0f, 32.0f, 39.0f));
    /* E: half of D#, up to F's edge. */
    CHECK (k.keyBounds (64) == juce::Rectangle<float> (0.0f, 181.0f, 32.0f, 29.0f));
    /* F: from E's edge to the middle of F#. */
    CHECK (k.keyBounds (65) == juce::Rectangle<float> (0.0f, 151.0f, 32.0f, 29.0f));

    /* Marks at each key's front, on its row. */
    CHECK (k.markBounds (60).getCentre() == juce::Point<float> (27.0f, 270.0f));
    CHECK (k.markBounds (61).getCentre().x == doctest::Approx (32.0f * 0.6f - 5.0f));
    CHECK (k.markBounds (61).getCentre().y == doctest::Approx (250.0f));
    CHECK (k.markBounds (60).getWidth() == doctest::Approx (PianoAxis::markSize));
}

TEST_CASE ("piano axis: nothing past MIDI's ends; zoomed right out, no gaps, names or marks")
{
    PianoAxis keys;
    keys.setBounds (0, 0, 32, 400);
    keys.setAxis (view (-0.5, 127.5));
    CHECK (keys.keyBounds (-1).isEmpty());
    CHECK (keys.keyBounds (128).isEmpty());
    CHECK (keys.rowBounds (128).isEmpty());
    CHECK (keys.markBounds (-1).isEmpty());
    /* 400px for 128 rows: 3.1px each, too thin for a mark or a gap. */
    CHECK (keys.markBounds (60).isEmpty());
    CHECK (keys.keyBounds (60).getHeight() == doctest::Approx (1.5f * 400.0f / 128.0f));

    /* A scale on keys too thin for a mark paints no mark. */
    keys.setScale (cMajor, 0);
    const auto image = ni::ui::test::render (keys);
    const auto row = keys.rowBounds (62);
    CHECK (near (image.getPixelAt (32 - 5, (int) row.getCentreY()), c::bg300));
}

TEST_CASE ("piano axis: lit keys in uv (uv-deep dimmed), the scale's dots, the tonic's in ink")
{
    Axis20 a;
    auto& k = a.keys;
    ni::ui::music::NoteSet lit;
    lit.set (64);   // E
    lit.set (61);   // C#
    k.setLit (lit);
    k.setScale (cMajor, 0);
    CHECK (k.getLit() == lit);
    CHECK (k.getScale() == cMajor);
    CHECK (k.getTonic() == 0);

    const auto pixel = [&] (const juce::Image& image, juce::Point<float> p) {
        return image.getPixelAt ((int) p.x, (int) p.y);
    };
    /* The front of a white key, clear of its mark and name. */
    const auto front = [&] (int m) { return juce::Point<float> (24.0f, k.rowBounds (m).getY() + 3.0f); };

    auto image = ni::ui::test::render (k);
    CHECK (near (pixel (image, front (64)), c::uv));                                   // E lit
    CHECK (near (pixel (image, front (62)), c::bg300));                                // D not
    CHECK (near (pixel (image, k.keyBounds (61).getCentre()), c::uv));                  // C# lit
    CHECK (near (pixel (image, k.keyBounds (63).getCentre()), c::bg000));               // D# not
    CHECK (near (pixel (image, k.markBounds (62).getCentre()), c::inkDim));             // D in the scale
    CHECK (near (pixel (image, k.markBounds (60).getCentre()), c::ink));                // C, the tonic
    CHECK (near (pixel (image, k.markBounds (64).getCentre()), c::onUv));               // E: lit and in it
    CHECK (near (pixel (image, k.markBounds (63).getCentre()), c::bg000));              // D#: not in it
    CHECK (near (pixel (image, k.markBounds (66).getCentre()), c::bg000));              // F#: not in it
    /* The gap above a white key, where no key is: what is under the axis. */
    CHECK (pixel (image, { 24.0f, k.keyBounds (60).getY() - 0.5f }).getAlpha() == 255);
    CHECK (near (pixel (image, { 24.0f, k.keyBounds (60).getY() - 0.5f }), c::bg000));

    k.setDimmed (true);
    CHECK (k.isDimmed());
    image = ni::ui::test::render (k);
    CHECK (near (pixel (image, front (64)), c::uvDeep));
    CHECK (near (pixel (image, k.keyBounds (61).getCentre()), c::uvDeep));

    /* No scale, no marks; a tonic outside 0..11 is none. */
    k.setScale (0, 0);
    image = ni::ui::test::render (k);
    CHECK (near (pixel (image, k.markBounds (62).getCentre()), c::bg300));
    k.setScale (cMajor, 12);
    CHECK (k.getTonic() == -1);
    image = ni::ui::test::render (k);
    CHECK (near (pixel (image, k.markBounds (60).getCentre()), c::inkDim));
}

TEST_CASE ("piano axis: it takes no pointer and no keyboard")
{
    PianoAxis k;
    CHECK_FALSE (k.getWantsKeyboardFocus());
    bool self = true, children = true;
    k.getInterceptsMouseClicks (self, children);
    CHECK_FALSE (self);
    CHECK_FALSE (children);
    CHECK (k.getTitle() == "Piano");
    CHECK (k.getWidth() == PianoAxis::defaultWidth);
}

TEST_CASE ("key paint: lit is uv, or uv-deep dimmed")
{
    CHECK (ni::ui::keypaint::litColour (false) == c::uv);
    CHECK (ni::ui::keypaint::litColour (true) == c::uvDeep);
}

NI_SNAPSHOT_TEST ("piano axis: three zooms beside the pitch rows")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("music-piano-axis")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "music-piano-axis");
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The single filters' editor, for every product, against a model with no
 * audio behind it (single-filter_fakes.h): the window and its grid, only
 * the product's controls with Output in the last column, what each gesture
 * on the picture asks of the host -- the dot's drag one gesture per
 * parameter, Alt for Q on a gain type, an edge tick for the width, the
 * wheel, the caption's two clicks -- every line the window writes, and each
 * window as a picture.
 *
 * What only the real processor can show -- the parameters being the host's,
 * the sound -- is tests/single-filter/sf_processor.cpp's.
 */
#include "Editor.h"
#include "InfoLines.h"
#include "Pointer.h"
#include "checks.h"
#include "single-filter_fakes.h"
#include "snapshot.h"

#include <doctest.h>

using namespace ni::sf;
using ni::sf::test::FakeModel;
using ni::ui::gallery::Pointer;

namespace
{
struct Rig
{
    double ms = 0.0;
    FakeModel model;
    Editor editor { model, [this] { return ms; } };

    explicit Rig (int s) : model (s) { frames (1); model.params.clear(); }

    void frames (int n = 1)
    {
        for (int i = 0; i < n; ++i)
        {
            ms += 1000.0 / 60.0;
            editor.tick (ms);
        }
    }

    double plain (int f) { return model.plain (model.indexOf (f)); }

    /* Field `f` to `v`; a field the product lacks is skipped. */
    void set (int f, double v)
    {
        if (model.indexOf (f) < 0)
            return;
        auto& p = model.parameter (model.indexOf (f));
        p.setValueNotifyingHost (p.convertTo0to1 ((float) v));
    }
};

const struct
{
    int shape;
    const char* key;
} all[] = {
    { shape::highPass, "high-pass" }, { shape::lowPass, "low-pass" },   { shape::bandPass, "band-pass" },
    { shape::notch, "notch" },        { shape::bell, "bell" },          { shape::lowShelf, "low-shelf" },
    { shape::highShelf, "high-shelf" }, { shape::tilt, "tilt" },        { shape::allPass, "all-pass" },
};
} // namespace

TEST_CASE ("single-filter: each window is its design's size, on its grid, and every line fits the bar")
{
    for (const auto& prod : all)
    {
        CAPTURE (prod.key);
        Rig rig (prod.shape);
        auto& e = rig.editor;
        CHECK (e.getWidth() == 664);
        CHECK (e.getHeight() == 572);
        CHECK (e.well().getBounds() == juce::Rectangle<int> (0, 0, 600, Editor::wellH));
        CHECK (e.panel().getBounds() == juce::Rectangle<int> (0, Editor::panelY, 600, Editor::panelH));
        CHECK (e.panel().getTitleText() == rig.model.productName());
        CHECK (e.knobFor (field::output)->getRight() == e.panel().contentBounds().getRight());
        CHECK (e.knobFor (field::freq)->getX() == e.panel().contentBounds().getX());
        CHECK (e.bypass().getRight() == e.panel().contentBounds().getRight());
        NI_CHECK_INFO_LIMIT (e);
        CHECK (e.well().getCaption().startsWith (prod.shape == shape::allPass ? "PHASE   STEREO (MID)" : "RESPONSE   STEREO (MID)"));
    }
}

TEST_CASE ("single-filter: the dot drags Freq and Gain as one gesture each; Alt drags Q")
{
    Rig rig (shape::bell);
    auto& w = rig.editor.well();
    auto& h = w.handle();
    const auto centre = [&] { return h.getBounds().toFloat().getCentre(); };
    CHECK (centre().x == doctest::Approx (w.xOf (1000.0)).epsilon (0.001));
    CHECK (centre().y == doctest::Approx (w.yOf (0.0)).epsilon (0.001));

    /* An octave right and 6 dB up. */
    const float octave = w.plotArea().getWidth() / (float) std::log2 (22000.0 / 10.0);
    const auto from = centre();
    {
        Pointer p;
        p.down (h, h.getLocalPoint (&w, from));
        p.drag (h.getLocalPoint (&w, from + juce::Point<float> (octave, w.yOf (6.0) - w.yOf (0.0))));
        p.up();
    }
    CHECK (rig.plain (field::freq) == doctest::Approx (2000.0).epsilon (0.01));
    CHECK (rig.plain (field::gain) == doctest::Approx (6.0).epsilon (0.01));
    const int fi = rig.model.indexOf (field::freq), gi = rig.model.indexOf (field::gain), qi = rig.model.indexOf (field::q);
    CHECK (rig.model.params.log().startsWith ("begin " + juce::String (fi) + ", begin " + juce::String (gi)));
    CHECK (rig.model.params.log().endsWith ("end " + juce::String (fi) + ", end " + juce::String (gi)));

    /* The dot follows, on the curve. */
    rig.frames (1);
    CHECK (centre().y == doctest::Approx (w.yOf (6.0)).epsilon (0.01));

    /* Alt at the press: up is Q, and Gain stays. */
    rig.model.params.clear();
    const auto at = centre();
    {
        Pointer p;
        p.down (h, h.getLocalPoint (&w, at), juce::ModifierKeys::altModifier);
        p.drag (h.getLocalPoint (&w, at + juce::Point<float> (0.0f, -48.0f)));
        p.up();
    }
    CHECK (rig.plain (field::q) == doctest::Approx (2.0 * 0.7071).epsilon (0.01));
    CHECK (rig.plain (field::gain) == doctest::Approx (6.0).epsilon (0.01));
    CHECK (rig.model.params.log().startsWith ("begin " + juce::String (fi) + ", begin " + juce::String (qi)));
}

TEST_CASE ("single-filter: a cut's dot drags Q up and down; a tilt's has no Q")
{
    Rig rig (shape::lowPass);
    auto& w = rig.editor.well();
    auto& h = w.handle();
    const auto from = h.getBounds().toFloat().getCentre();
    CHECK (from.y == doctest::Approx (w.yOf (-3.0103)).epsilon (0.01));
    {
        Pointer p;
        p.down (h, h.getLocalPoint (&w, from));
        p.drag (h.getLocalPoint (&w, from + juce::Point<float> (0.0f, -96.0f)));
        p.up();
    }
    CHECK (rig.plain (field::q) == doctest::Approx (4.0 * 0.7071).epsilon (0.01));
    CHECK (h.altYBinding() == nullptr);
    Rig tilt (shape::tilt);
    CHECK (tilt.editor.well().handle().altYBinding() == nullptr);
    CHECK (tilt.editor.well().handle().yBinding() != nullptr);
}

TEST_CASE ("single-filter: a bell's edge ticks set Q; the wheel is Q")
{
    Rig rig (shape::bell);
    auto& w = rig.editor.well();
    const auto& pic = w.shownPicture();
    REQUIRE_FALSE (std::isnan (pic.edgeHz[1]));
    const auto hy = w.handleAt().y;
    {
        Pointer p;
        p.down (w, { w.xOf (pic.edgeHz[1]), hy });
        p.drag ({ w.xOf (1000.0 * std::sqrt (2.0)), hy });
        p.up();
    }
    CHECK (rig.plain (field::q) == doctest::Approx (1.414).epsilon (0.01));
    const double q = rig.plain (field::q);
    Pointer::wheel (w, { w.xOf (300.0), w.yOf (0.0) }, 0.5f);
    CHECK (rig.plain (field::q) == doctest::Approx (q * 2.0).epsilon (0.01));
}

TEST_CASE ("single-filter: the caption's right end sets the analyzer and the range")
{
    Rig rig (shape::highShelf);
    auto& w = rig.editor.well();
    const float right = (float) w.getWidth() - 8.0f;
    CHECK (w.rangeDb() == 18);
    {
        Pointer p;
        p.click (w, { right, 8.0f });
    }
    CHECK (w.rangeDb() == 30);
    CHECK (rig.plain (field::analyzer) == 3.0);
    {
        Pointer p;
        p.click (w, { right - 70.0f, 8.0f });
    }
    CHECK (rig.plain (field::analyzer) == 2.0);
}

TEST_CASE ("single-filter: Slope and Family snap to what plays; what is unused is disabled")
{
    Rig rig (shape::highPass);
    auto& e = rig.editor;
    rig.set (field::order, 0.0);
    rig.frames (1);
    CHECK_FALSE (e.knobFor (field::q)->isEnabled());
    CHECK_FALSE (e.knobFor (field::ripple)->isEnabled());
    CHECK_FALSE (e.knobFor (field::stopband)->isEnabled());
    e.selectFor (field::family)->onChange (5);
    CHECK (rig.plain (field::order) == 1.0);
    rig.frames (1);
    CHECK (e.knobFor (field::q)->isEnabled());
    e.selectFor (field::family)->onChange (4);
    rig.frames (1);
    CHECK (e.knobFor (field::ripple)->isEnabled());
    CHECK (e.knobFor (field::stopband)->isEnabled());
    CHECK (e.toggleFor (field::matched) != nullptr);
    Rig bell (shape::bell);
    CHECK (bell.editor.toggleFor (field::adaptiveQ) != nullptr);
    CHECK (bell.editor.selectFor (field::family) == nullptr);
    CHECK (bell.editor.knobFor (field::q)->isEnabled());
}

NI_SNAPSHOT_TEST ("single-filter: each window at its defaults")
{
    for (const auto& prod : all)
    {
        CAPTURE (prod.key);
        Rig rig (prod.shape);
        /* What the board opens each with: something to see. */
        rig.set (field::gain, prod.shape == shape::highShelf ? -3.0 : prod.shape == shape::bell ? 6.0 : 4.0);
        rig.set (field::order, prod.shape == shape::highPass || prod.shape == shape::lowPass ? 3.0 : 1.0);
        rig.set (field::q, prod.shape == shape::notch ? 4.0 : prod.shape == shape::bandPass ? 2.0 : 0.7071);
        rig.frames (2);
        NI_CHECK_SNAPSHOT (rig.editor, juce::String ("single-filter-") + prod.key);
    }
}

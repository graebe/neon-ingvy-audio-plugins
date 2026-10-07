// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The hint bar: what it shows by the precedence the card gives, the tips
 * truncating in a cell that takes all the room the window leaves them, and
 * the two things that never move -- the Signature closing the bar and the
 * Motion switch 16px before it.
 */
#include "Hint.h"

#include "Info.h"
#include "Toggle.h"
#include "UvTokens.h"
#include "checks.h"
#include "pages.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Clause;
using ni::ui::Hint;
namespace c = uv::tok::colour;

namespace
{
const std::vector<Clause> conventions { { "click", "a step to toggle" },
                                        { "shift-click", "for a tie" },
                                        { "drag", "up or down for its amount" } };

const juce::String rate = juce::String::fromUTF8 ("Rate \xe2\x80\x94 the length of one step, synced to the song tempo.");

/* A bar in a window of `width`, with a Motion switch, as an EditorFrame
 * builds one. */
struct Bar
{
    ni::ui::Toggle motion { "Motion" };
    Hint hint;

    explicit Bar (int width = 824)
    {
        motion.setOn (true);
        motion.setSize (motion.idealWidth(), (int) uv::tok::size::controlH);
        hint.setConventions (conventions);
        hint.setMotionSwitch (&motion);
        hint.setSize (width, Hint::height);
    }
};

/* The brightest pixel in a column range of a rendered bar's text line. */
float brightest (const juce::Image& img, int x0, int x1)
{
    float best = 0.0f;
    for (int y = Hint::lineTop; y < Hint::lineTop + 14; ++y)
        for (int x = juce::jmax (0, x0); x < juce::jmin (img.getWidth(), x1); ++x)
            best = juce::jmax (best, img.getPixelAt (x, y).getBrightness());
    return best;
}
} // namespace

TEST_CASE ("hint: the rule, the line and the signature where the card puts them")
{
    Bar bar;
    CHECK (bar.hint.getHeight() == 28);

    const auto img = ni::ui::test::render (bar.hint);
    for (int x : { 0, 400, 823 })
        CHECK (img.getPixelAt (x, 0) == c::line100);   // the rule, the full width

    /* The Signature closes the bar inside the window's padding, level with
     * the text. */
    const auto sig = bar.hint.signature().getBounds();
    CHECK (sig.getRight() == 824 - 32);
    CHECK (sig.getY() == Hint::lineTop);
    CHECK (bar.hint.tipsBounds().getX() == 32);
    CHECK (bar.hint.tipsBounds().getY() == Hint::lineTop);
}

TEST_CASE ("hint: the tips take all the room to space-4 before the switch, which sits space-4 before the signature")
{
    /* .tips { flex: 1; min-width: 0 }: the window's width places them, the
     * conventions do not -- a line has all the room there is. */
    for (const auto& clauses : { conventions, std::vector<Clause> { { "pick", "a bus" } }, std::vector<Clause> {} })
    {
        CAPTURE (clauses.size());
        Bar bar;
        bar.hint.setConventions (clauses);
        const auto tips = bar.hint.tipsBounds();
        CHECK (tips.getRight() + Hint::gap == bar.hint.motionBounds().getX());
        CHECK (bar.hint.motionBounds().getRight() + Hint::gap == bar.hint.signature().getX());
        CHECK (tips.getWidth() > (int) std::ceil (ni::ui::clausesWidth (conventions)));
    }

    /* Its 14px housing is centred on the text's line. */
    Bar bar;
    CHECK (bar.motion.getBounds().getCentreY() == Hint::lineTop + 7);
}

TEST_CASE ("hint: neither the switch nor the signature moves while a line or an outcome comes and goes")
{
    Bar bar;
    const auto motion = bar.hint.motionBounds();
    const auto sig = bar.hint.signature().getBounds();

    bar.hint.setInfoLine (rate);
    CHECK (bar.hint.motionBounds() == motion);
    bar.hint.setOutcome (Clause { "Copied", "slot 1, which was a longer outcome than any convention." });
    CHECK (bar.hint.motionBounds() == motion);
    bar.hint.setInfoLine ({});
    bar.hint.setOutcome (std::nullopt);
    CHECK (bar.hint.motionBounds() == motion);
    CHECK (bar.hint.signature().getBounds() == sig);
}

TEST_CASE ("hint: in a narrow window the tips give way, never the 16px before the signature")
{
    for (int width : { 600, 400, 360, 300 })
    {
        CAPTURE (width);
        Bar bar (width);
        const auto tips = bar.hint.tipsBounds();
        const auto motion = bar.hint.motionBounds();
        const auto sig = bar.hint.signature().getBounds();
        CHECK (sig.getX() - motion.getRight() == 16);
        CHECK (motion.getX() - tips.getRight() == 16);
        CHECK (tips.getWidth() < (int) std::ceil (ni::ui::clausesWidth (conventions)));

        /* No text between the tips' cell and the switch -- only the lit
         * switch's halo reaches there: the line is cut inside its cell, with
         * an ellipsis. */
        const auto img = ni::ui::test::render (bar.hint);
        CHECK (brightest (img, tips.getRight() + 1, motion.getX()) < 0.2f);
        CHECK (brightest (img, tips.getX(), tips.getRight()) > 0.85f);
    }
}

TEST_CASE ("hint: without a Ground there is no switch, and the tips run to 16px before the signature")
{
    Hint hint;
    hint.setConventions (conventions);
    hint.setSize (360, Hint::height);
    CHECK (hint.motionBounds().isEmpty());
    CHECK (hint.signature().getX() - hint.tipsBounds().getRight() == 16);
}

TEST_CASE ("hint: an outcome first, then the line under the pointer, then the conventions")
{
    Hint hint;
    hint.setConventions (conventions);

    auto shown = hint.content();
    CHECK (shown.clauses == conventions);
    CHECK_FALSE (shown.info.has_value());

    hint.setInfoLine (rate);
    shown = hint.content();
    REQUIRE (shown.info.has_value());
    CHECK (shown.info->name == "Rate");
    CHECK (shown.clauses == conventions);   // held underneath

    hint.setOutcome (Clause { "Copied", "slot 1." });
    shown = hint.content();
    CHECK_FALSE (shown.info.has_value());
    REQUIRE (shown.clauses.size() == 3);
    CHECK (shown.clauses[0].name == "Copied");
    CHECK (shown.clauses[1] == conventions[0]);
}

TEST_CASE ("hint: the verb in ink, the rest in ink-muted, an info line laid over the conventions")
{
    Bar bar;
    auto img = ni::ui::test::render (bar.hint);
    /* "click" starts the line: a pixel of it reaches ink. */
    const float verb = brightest (img, 32, 60);
    CHECK (verb > 0.85f);

    /* With a line on show, the conventions are not drawn: where "shift-click"
     * was, the line's muted rest is. */
    bar.hint.setInfoLine (rate);
    img = ni::ui::test::render (bar.hint);
    CHECK (brightest (img, 32, 52) > 0.85f);                  // "Rate", in ink
    CHECK (brightest (img, 80, 200) < verb);                  // "— the length ...", muted
}

TEST_CASE ("hint: a screen reader reads what is on show")
{
    Hint hint;
    hint.setConventions (conventions);
    const auto handler = hint.createAccessibilityHandler();
    CHECK (handler->getRole() == juce::AccessibilityRole::staticText);
    CHECK (hint.getTitle().startsWith ("click a step to toggle"));

    hint.setInfoLine (rate);
    CHECK (hint.getTitle() == rate);
}

TEST_CASE ("hint: the bar carries the signature once, and its own lines keep the limit")
{
    Bar bar;
    ni::ui::setInfo (bar.motion, juce::String::fromUTF8 ("Motion \xe2\x80\x94 let the music ripple the background; remembered on this Mac."));
    ni::ui::setInfo (bar.hint.signature(), juce::String::fromUTF8 ("Neon Ingvy \xe2\x80\x94 the publisher of this plugin."));

    int signatures = 0;
    for (auto* child : bar.hint.getChildren())
        signatures += dynamic_cast<ni::ui::Signature*> (child) != nullptr ? 1 : 0;
    CHECK (signatures == 1);
    NI_CHECK_INFO_LIMIT (bar.hint);
}

NI_SNAPSHOT_TEST ("hint: conventions, a line, an outcome, and a narrow bar")
{
    NI_CHECK_PAGE ("display-hint", "hint-states");
}

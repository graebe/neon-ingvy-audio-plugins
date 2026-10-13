// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Panel: its look, where its controls go in each form, and its title as
 * the place its info line lives.
 */
#include "Panel.h"

#include "Info.h"
#include "UvTokens.h"
#include "pages.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Panel;
namespace c = uv::tok::colour;

TEST_CASE ("panel: bg-100 inside a line-100 hairline")
{
    Panel p ("Gate");
    p.setSize (200, 90);
    const auto img = ni::ui::test::render (p);
    CHECK (img.getPixelAt (0, 50) == c::line100);
    CHECK (img.getPixelAt (199, 50) == c::line100);
    CHECK (img.getPixelAt (100, 89) == c::line100);
    CHECK (img.getPixelAt (100, 70) == c::bg100);
}

TEST_CASE ("panel: standard, the title over the controls")
{
    Panel p ("Gate");
    p.setSize (258, 90);
    /* 1px hairline + 16 padding; the title's 16px line and 12 more. */
    CHECK (p.titleComponent().getBounds() == juce::Rectangle<int> (17, 17, 224, 16));
    CHECK (p.contentBounds() == juce::Rectangle<int> (17, 45, 224, 28));

    /* The title is drawn: ink-muted pixels in its line, none under it. */
    const auto img = ni::ui::test::render (p);
    float lit = 0.0f;
    for (int x = 17; x < 60; ++x)
        lit = juce::jmax (lit, img.getPixelAt (x, 25).getBrightness());
    CHECK (lit > 0.5f);
}

TEST_CASE ("panel: compact, 140 tall, the title up the left edge and centred on it")
{
    Panel p ("Gate", Panel::Form::compact);
    p.setSize (270, Panel::compactHeight);
    const auto title = p.titleComponent().getBounds();
    CHECK (title.getX() == 17);
    CHECK (title.getWidth() == 16);
    CHECK (title.getCentreY() == 70);
    CHECK (title.getHeight() > 30);   // GATE, tracked, read upwards
    CHECK (p.contentBounds() == juce::Rectangle<int> (45, 17, 270 - 45 - 17, 106));

    /* An untitled panel's controls start at the padding. */
    Panel bare;
    bare.setSize (100, 60);
    CHECK (bare.contentBounds() == juce::Rectangle<int> (17, 17, 66, 26));
    CHECK_FALSE (bare.titleComponent().isVisible());
}

TEST_CASE ("panel: the title carries the panel's line; the panel is a named group")
{
    Panel p ("Gate", Panel::Form::compact);
    const auto line = juce::String::fromUTF8 ("Gate \xe2\x80\x94 when the steps fall and how much of each one sounds.");
    p.setTitleInfo (line);

    CHECK (ni::ui::infoOf (p.titleComponent()) == line);
    CHECK (ni::ui::infoSource (&p.titleComponent()) == &p.titleComponent());
    CHECK (p.getDescription() == line);
    CHECK (ni::ui::collectInfo (p) == std::vector<juce::String> { line });

    const auto handler = p.createAccessibilityHandler();
    CHECK (handler->getRole() == juce::AccessibilityRole::group);
    CHECK (handler->getTitle() == "Gate");
    CHECK_FALSE (p.titleComponent().isAccessible());

    p.setTitleText ("Fade");
    CHECK (p.getTitle() == "Fade");
}

NI_SNAPSHOT_TEST ("panel: standard and compact")
{
    NI_CHECK_PAGE ("display-panel", "panel-forms");
}

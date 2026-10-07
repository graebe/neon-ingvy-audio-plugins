// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Icon component: the glyph centred in its box in the colour its control
 * gives it, and nothing a pointer or a screen reader can land on.
 */
#include "Icon.h"

#include "UvIcons.h"
#include "UvTokens.h"
#include "events.h"
#include "pages.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Icon;
namespace c = uv::tok::colour;

TEST_CASE ("icon: decorative to the pointer and to assistive technology")
{
    Icon icon ("copy");
    CHECK (icon.getWidth() == Icon::size);
    CHECK (icon.getHeight() == Icon::size);

    bool children = true, self = true;
    icon.getInterceptsMouseClicks (self, children);
    CHECK_FALSE (self);
    CHECK_FALSE (children);
    CHECK_FALSE (icon.isAccessible());
}

TEST_CASE ("icon: the glyph is 16px, centred in its box, in its control's colour")
{
    /* stop: a 9px square outline from 3.5 to 12.5 on its grid. In a 28px
     * icon-only button the grid starts at 6, so the outline's left edge is at
     * 9.5 and the middle of the square is empty. */
    Icon icon ("stop", c::ink);
    icon.setSize (28, 28);
    auto img = ni::ui::test::render (icon);

    const auto edge = img.getPixelAt (9, 14);
    CHECK (edge.getBrightness() > 0.5f);
    CHECK (img.getPixelAt (14, 14) == c::bg000);
    CHECK (img.getPixelAt (2, 2) == c::bg000);

    /* currentColor: on a lit button the same glyph is on-uv. */
    icon.setTint (c::onUv);
    img = ni::ui::test::render (icon);
    const auto lit = img.getPixelAt (9, 14);
    CHECK (lit.getBlue() > lit.getGreen());
    CHECK (lit.getBrightness() < edge.getBrightness());
}

TEST_CASE ("icon: a glyph can be changed or removed")
{
    Icon icon;
    CHECK (icon.getGlyph().isEmpty());
    icon.setGlyph ("power");
    CHECK (icon.getGlyph() == "power");

    icon.setGlyph ({});
    const auto img = ni::ui::test::render (icon);
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
            REQUIRE (img.getPixelAt (x, y) == c::bg000);
}

NI_SNAPSHOT_TEST ("icon: the set, each glyph over its name")
{
    NI_CHECK_PAGE ("display-icon", "icon-set");
}

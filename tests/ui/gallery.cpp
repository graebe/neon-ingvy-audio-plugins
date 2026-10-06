// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The gallery's registry, and the picture of its empty frame -- the snapshot
 * that proves the harness: tokens, the embedded faces, the ground and the
 * software renderer, in one image that no component can change.
 */
#include "Gallery.h"
#include "checks.h"
#include "snapshot.h"

using ni::ui::gallery::Frame;
using ni::ui::gallery::Page;

TEST_CASE ("gallery: pages sort Foundation, Controls, Display, then the rest")
{
    const auto make = [] { return std::make_unique<juce::Component>(); };
    std::vector<Page> list { { "Zeta", "b", make }, { "Display", "a", make },
                             { "Controls", "b", make }, { "Foundation", "z", make },
                             { "Controls", "a", make } };
    for (auto& p : list)
        ni::ui::gallery::add (p);

    std::vector<juce::String> order;
    for (const auto& p : ni::ui::gallery::pages())
        if (p.name.length() == 1)
            order.push_back (p.group + "/" + p.name);

    CHECK (order == std::vector<juce::String> { "Foundation/z", "Controls/a", "Controls/b",
                                                "Display/a", "Zeta/b" });
}

TEST_CASE ("gallery: a page is found by its id or its name")
{
    const auto make = [] { auto c = std::make_unique<juce::Component>(); c->setSize (10, 10); return c; };
    Frame frame ({ { "Controls", "Knob", make }, { "Display", "Plot Well", make } });

    CHECK (ni::ui::gallery::idOf ({ "Display", "Plot Well", make }) == "display-plot-well");
    CHECK (frame.show ("display-plot-well"));
    CHECK (frame.current() == 1);
    CHECK (frame.show ("knob"));
    CHECK (frame.current() == 0);
    CHECK (frame.page() != nullptr);
    CHECK (frame.page()->getPosition() == frame.pageArea().getPosition());
    CHECK_FALSE (frame.show ("nothing"));
}

NI_SNAPSHOT_TEST ("gallery: the empty frame")
{
    Frame frame ({});
    REQUIRE (frame.getWidth() == Frame::width);
    NI_CHECK_SNAPSHOT (frame, "gallery-empty");
}

/* ---------------------------------------------------------------- pages -- */

/*
 * Every page's info lines are within the limit: a component page sets the
 * lines its control would carry, so the kit's strings are held here without
 * anyone remembering to.
 */
TEST_CASE ("gallery: no page carries an info line over 72 characters")
{
    for (const auto& page : ni::ui::gallery::pages())
    {
        CAPTURE (ni::ui::gallery::idOf (page));
        const auto component = page.make();
        REQUIRE (component != nullptr);
        NI_CHECK_INFO_LIMIT (*component);
    }
}

/* The foundation's pages, as goldens: the design layer's whole picture. */
NI_SNAPSHOT_TEST ("gallery: the foundation pages")
{
    for (const auto* name : { "Colour", "Type", "Icons", "Light", "Stock widgets" })
    {
        CAPTURE (name);
        Frame frame (ni::ui::gallery::pages());
        REQUIRE (frame.show (juce::String (name)));
        REQUIRE (frame.page() != nullptr);

        /* The page as the window shows it, on the ground. */
        const auto picture = frame.createComponentSnapshot (frame.page()->getBounds(), true, 1.0f,
                                                            juce::SoftwareImageType());
        const auto r = ni::ui::test::compare (picture, "foundation-" + juce::String (name).toLowerCase().replaceCharacter (' ', '-'));
        CHECK_MESSAGE (r.ok, r.message.toStdString());
    }
}

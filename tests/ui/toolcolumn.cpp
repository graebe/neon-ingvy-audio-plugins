// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The ToolColumn: its buttons 28px square, 12 apart and centred across it;
 * exactly one on, a click picking another and saying so, a click on the one
 * on saying nothing; each its title and its line; Space left to the host.
 * Then its gallery page, as a picture.
 */
#include "Gallery.h"
#include "Info.h"
#include "Pointer.h"
#include "ToolColumn.h"
#include "checks.h"
#include "hostkeys.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::ToolColumn;
using ni::ui::gallery::Pointer;

namespace
{
std::vector<ToolColumn::Tool> threeTools()
{
    const auto line = [] (const char* utf8) { return juce::String::fromUTF8 (utf8); };
    return { { "Play", "play", line ("Play — the first tool.") },
             { "Loop", "loop", line ("Loop — the second tool.") },
             { "Link", "link", line ("Link — the third tool.") } };
}
} // namespace

TEST_CASE ("tool column: 28px buttons, 12 apart, centred across it")
{
    ToolColumn tools;
    tools.setTools (threeTools());
    CHECK (tools.size() == 3);
    CHECK (tools.idealSize() == juce::Rectangle<int> (0, 0, 28, 3 * 28 + 2 * 12));
    tools.setBounds (0, 0, 36, tools.idealSize().getHeight());
    for (int i = 0; i < 3; ++i)
    {
        CAPTURE (i);
        CHECK (tools.button (i).getBounds() == juce::Rectangle<int> (4, 40 * i, 28, 28));
        CHECK (tools.button (i).isIconOnly());
    }
    CHECK (tools.button (1).getIcon() == "loop");
    CHECK (tools.button (1).getTitle() == "Loop");
    CHECK (ni::ui::infoOf (tools.button (2)) == juce::String::fromUTF8 ("Link — the third tool."));
    CHECK (tools.getTitle() == "Tools");
    ToolColumn none;
    CHECK (none.idealSize().isEmpty());
}

TEST_CASE ("tool column: one is on; a click picks another and says so, the one on says nothing")
{
    ToolColumn tools;
    tools.setTools (threeTools());
    tools.setBounds (0, 0, 28, tools.idealSize().getHeight());
    std::vector<int> picked;
    tools.onChange = [&] (int i) { picked.push_back (i); };
    CHECK (tools.getSelected() == 0);
    CHECK (tools.button (0).isOn());
    CHECK_FALSE (tools.button (1).isOn());

    Pointer p;
    p.click (tools.button (2), { 14.0f, 14.0f });
    CHECK (picked == std::vector<int> { 2 });
    CHECK (tools.getSelected() == 2);
    CHECK ((! tools.button (0).isOn() && tools.button (2).isOn()));
    p.click (tools.button (2), { 14.0f, 14.0f });
    CHECK (picked.size() == 1);

    /* Picked by the caller: lit, and nobody told. */
    tools.setSelected (1);
    CHECK (tools.button (1).isOn());
    CHECK_FALSE (tools.button (2).isOn());
    tools.setSelected (7);
    CHECK (tools.getSelected() == 1);
    CHECK (picked.size() == 1);

    /* New tools start from the first. */
    tools.setTools (threeTools());
    CHECK (tools.getSelected() == 0);
    CHECK (tools.button (0).isOn());
}

TEST_CASE ("tool column: Space and Shift+Space are the host's, a tool focused or not")
{
    ToolColumn tools;
    tools.setTools (threeTools());
    tools.setBounds (0, 0, 28, tools.idealSize().getHeight());
    const auto users = ni::ui::test::spaceUsersAfterClicks (tools);
    CHECK_MESSAGE (users.isEmpty(), users.joinIntoString ("\n").toStdString());
}

NI_SNAPSHOT_TEST ("tool column: one lit, a tool hovered, pressed and focused")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("controls-tool-column")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "controls-tool-column");
}

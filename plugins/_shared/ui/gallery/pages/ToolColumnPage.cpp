// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The ToolColumn: one tool lit, beside it the states a pointer and a keyboard
 * give the others -- a column at rest, one hovered, one pressed, one focused.
 * The gallery has only the system's glyphs; a product's own (UvIcons.h) sit
 * in its column the same way.
 */
#include "Gallery.h"

#include "ControlsPage.h"
#include "Info.h"
#include "Pointer.h"
#include "ToolColumn.h"

namespace
{
using ni::ui::InfoText;
using ni::ui::ToolColumn;

constexpr InfoText playInfo { "Play — the first tool: what the pointer does in the well." };
constexpr InfoText loopInfo { "Loop — the second tool." };
constexpr InfoText shuffleInfo { "Shuffle — the third tool." };
constexpr InfoText linkInfo { "Link — the fourth tool." };

struct ToolColumnPage final : public ni::ui::gallery::ControlsPage
{
    ToolColumnPage()
    {
        heading ("Tools: one lit; hover, pressed, focus", 0, 0);
        const std::vector<ToolColumn::Tool> tools { { "Play", "play", playInfo }, { "Loop", "loop", loopInfo },
                                                    { "Shuffle", "shuffle", shuffleInfo }, { "Link", "link", linkInfo } };
        for (int i = 0; i < 2; ++i)
        {
            auto& column = *columns.add (new ToolColumn());
            column.setTools (tools);
            const auto size = column.idealSize();
            column.setBounds (i * 64, 24, 36, size.getHeight());
            addAndMakeVisible (column);
        }
        columns[1]->setSelected (2);
        ni::ui::gallery::Pointer pointer;
        pointer.enter (columns[1]->button (0), { 14.0f, 14.0f });
        pointer.down (columns[1]->button (1), { 14.0f, 14.0f });
        showFocus (columns[1]->button (3));

        note ("a click picks a tool; the one lit stays lit", 0, 24 + columns[0]->getHeight() + 16);
        setSize (360, 24 + columns[0]->getHeight() + 40);
    }

    juce::OwnedArray<ToolColumn> columns;
};

} // namespace

NI_GALLERY_PAGE ("Controls", "Tool Column", [] { return std::make_unique<ToolColumnPage>(); });

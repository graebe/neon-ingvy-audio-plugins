// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Icon component as its card shows the set: each glyph in ink over its
 * name, eight to a row. (The four colours a control gives a glyph are on the
 * Foundation's Icons page.)
 */
#include "DisplayPage.h"
#include "Gallery.h"

#include "Icon.h"
#include "UvIcons.h"

#include <memory>
#include <vector>

namespace
{

struct IconPage final : public ni::ui::gallery::DisplayPage
{
    static constexpr int cell = 80, perRow = 8, rowHeight = 48;

    IconPage()
    {
        const auto& names = uv::iconNames();
        const int rows = (names.size() + perRow - 1) / perRow;
        setSize (perRow * cell, rows * rowHeight);

        for (int i = 0; i < names.size(); ++i)
        {
            const int x = (i % perRow) * cell, y = (i / perRow) * rowHeight;
            auto& icon = *icons.emplace_back (std::make_unique<ni::ui::Icon> (names[i]));
            addAndMakeVisible (icon);
            icon.setTopLeftPosition (x + (cell - ni::ui::Icon::size) / 2, y);
        }
    }

    void paint (juce::Graphics& g) override
    {
        /* Each name under its glyph, centred: the card's column, gap 8px. */
        const auto& style = uv::tok::type::label;
        const auto& names = uv::iconNames();
        for (int i = 0; i < names.size(); ++i)
        {
            const float x = (float) ((i % perRow) * cell), y = (float) ((i / perRow) * rowHeight);
            uv::type::draw (g, uv::type::cased (style, names[i]),
                            { x, y + (float) ni::ui::Icon::size + 8.0f, (float) cell, style.lineHeight },
                            uv::type::font (style), uv::tok::colour::inkMuted, juce::Justification::centred);
        }
        ni::ui::paintChildLights (g, *this);
    }

    std::vector<std::unique_ptr<ni::ui::Icon>> icons;
};

} // namespace

NI_GALLERY_PAGE ("Display", "Icon", [] { return std::make_unique<IconPage>(); });

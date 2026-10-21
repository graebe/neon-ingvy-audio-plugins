// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The RadialMenu over a well, in the states a pointer and a keyboard give it:
 * at rest with the owner's words in the middle; a value pointed at, its text
 * in the middle; seven items with one disabled and one holding keyboard
 * focus; and one opened at a corner of its well, pushed back inside.
 *
 * The gallery has only the system's glyphs; a product's own (UvIcons.h) sit
 * in a disc the same way.
 */
#include "Gallery.h"

#include "ControlsPage.h"
#include "Pointer.h"
#include "RadialMenu.h"

namespace
{
using ni::ui::RadialMenu;
namespace c = uv::tok::colour;

using Item = RadialMenu::Item;

Item value (const char* id, const char* label, double v, const char* text)
{
    Item it;
    it.id = id;
    it.label = label;
    it.value01 = v;
    it.valueText = juce::String::fromUTF8 (text);
    return it;
}

Item action (const char* id, const char* label, const char* glyph)
{
    Item it;
    it.id = id;
    it.label = label;
    it.icon = glyph;
    it.kind = Item::Kind::action;
    return it;
}

std::vector<Item> fiveItems()
{
    auto level = value ("level", "Level", 0.7, "\xe2\x88\x92" "3.0 dB");
    auto pan = value ("pan", "Pan", 0.62, "R24");
    pan.bipolar = true;
    auto rate = value ("rate", "Rate", 0.5, "1/8");
    rate.detents = { 0.25, 0.5, 0.75 };
    auto shuffle = value ("shuffle", "Shuffle", 0.3, "30 %");
    shuffle.icon = "shuffle";
    shuffle.shiftIsAlternate = true;
    return { level, pan, rate, shuffle, action ("copy", "Copy", "copy") };
}

std::vector<Item> sevenItems()
{
    auto items = fiveItems();
    items[2].enabled = false;
    items.push_back (action ("reset", "Reset", "reset"));
    items.push_back (action ("link", "Link", "link"));
    return items;
}

struct RadialMenuPage final : public ni::ui::gallery::ControlsPage
{
    RadialMenuPage()
    {
        const char* headings[] = { "At rest", "A value pointed at", "Seven: one off, keyboard focus",
                                   "Opened at a corner" };
        for (int i = 0; i < 4; ++i)
        {
            const int x = (i % 2) * 344, y = (i / 2) * 272;
            heading (headings[i], x, y);
            wells.push_back (juce::Rectangle<int> (x, y + 24, 336, 236));
            auto& menu = *menus.add (new RadialMenu());
            addAndMakeVisible (menu);
        }

        menus[0]->setItems (fiveItems());
        menus[0]->setCentreText ("3 notes");
        menus[1]->setItems (fiveItems());
        menus[1]->setCentreText ("3 notes");
        menus[2]->setItems (sevenItems());
        menus[2]->setCentreText ("4 notes");
        menus[3]->setItems ({ fiveItems()[0], fiveItems()[1], fiveItems()[4] });
        menus[3]->setCentreText ("1 note");

        for (int i = 0; i < 3; ++i)
            menus[i]->placeAround (wells[(size_t) i].getCentre(), wells[(size_t) i]);
        corner = wells[3].getTopLeft() + juce::Point<int> (24, 24);
        menus[3]->placeAround (corner, wells[3]);

        ni::ui::gallery::Pointer pointer;
        pointer.enter (menus[1]->item (0), menus[1]->itemCentre (0) - menus[1]->item (0).getPosition().toFloat());
        showFocus (menus[2]->item (1));

        note ("a press outside goes to the well: it closes the menu and acts there", 0, 544);
        setSize (680, 560);
    }

    void paint (juce::Graphics& g) override
    {
        for (const auto& w : wells)
        {
            g.setColour (c::bg200);
            g.fillRect (w);
            g.setColour (c::line100);
            g.drawRect (w, 1);
        }
        /* Where the corner's menu was asked for. */
        g.setColour (c::inkDim);
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (corner.toFloat()));
        ControlsPage::paint (g);
    }

    std::vector<juce::Rectangle<int>> wells;
    juce::Point<int> corner;
    juce::OwnedArray<RadialMenu> menus;
};

} // namespace

NI_GALLERY_PAGE ("Controls", "Radial Menu", [] { return std::make_unique<RadialMenuPage>(); });

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The gallery's registry and its window content. Gallery.h has the rules.
 */
#include "Gallery.h"

#include "Luminous.h"
#include "UvGround.h"
#include "UvTokens.h"
#include "UvType.h"

#include <algorithm>

namespace ni::ui::gallery
{

namespace
{
namespace c = uv::tok::colour;
namespace sp = uv::tok::space;

/* Function-local, so a page registering itself during static
 * initialisation finds the list already made, whichever file runs first. */
std::vector<Page>& registry()
{
    static std::vector<Page> list;
    return list;
}

int groupRank (const juce::String& group)
{
    if (group == "Foundation") return 0;
    if (group == "Controls") return 1;
    if (group == "Display") return 2;
    return 3;
}

constexpr int pad = (int) sp::space4;
constexpr int rowHeight = (int) uv::tok::size::controlH;
} // namespace

bool add (Page page)
{
    jassert (page.make != nullptr && page.name.isNotEmpty());
    registry().push_back (std::move (page));
    return true;
}

std::vector<Page> pages()
{
    auto list = registry();
    std::stable_sort (list.begin(), list.end(), [] (const Page& a, const Page& b)
    {
        const int ra = groupRank (a.group), rb = groupRank (b.group);
        if (ra != rb) return ra < rb;
        if (a.group != b.group) return a.group < b.group;
        return a.name < b.name;
    });
    return list;
}

juce::String idOf (const Page& p)
{
    return (p.group + "-" + p.name).toLowerCase().replaceCharacter (' ', '-');
}

/* ================================================================ frame == */

Frame::Frame (std::vector<Page> list) : pages (std::move (list))
{
    setSize (width, height);
    setWantsKeyboardFocus (true);
    setOpaque (true);
    layoutRows();
}

Frame::~Frame() = default;

void Frame::layoutRows()
{
    rows.clear();

    /* Under the title (16) and the release line (14), a space-6 gap. */
    int y = pad + 16 + 14 + (int) sp::space6;
    juce::String group;
    for (size_t i = 0; i < pages.size(); ++i)
    {
        if (i == 0 || pages[i].group != group)
        {
            group = pages[i].group;
            if (i > 0)
                y += (int) sp::space2;
            rows.push_back ({ -1, group, { pad, y, listWidth - 2 * pad, 20 } });
            y += 20;
        }
        rows.push_back ({ (int) i, pages[i].name, { 0, y, listWidth - 1, rowHeight } });
        y += rowHeight;
    }
}

juce::Rectangle<int> Frame::pageArea() const
{
    return { listWidth + (int) sp::space8, (int) sp::space8,
             width - listWidth - 2 * (int) sp::space8, height - 2 * (int) sp::space8 };
}

void Frame::show (int index)
{
    if (content != nullptr)
        removeChildComponent (content.get());
    content.reset();

    selected = (index >= 0 && index < numPages()) ? index : -1;
    if (selected >= 0)
    {
        content = pages[(size_t) selected].make();
        if (content != nullptr)
        {
            addAndMakeVisible (*content);
            resized();
        }
    }
    repaint();
}

bool Frame::show (const juce::String& idOrName)
{
    for (int i = 0; i < numPages(); ++i)
    {
        const auto& p = pages[(size_t) i];
        if (idOf (p) == idOrName.toLowerCase() || p.name.equalsIgnoreCase (idOrName))
        {
            show (i);
            return true;
        }
    }
    return false;
}

void Frame::resized()
{
    if (content != nullptr)
    {
        /* A page keeps the size it gave itself, inside the area; one that
         * gave none fills it. */
        const auto area = pageArea();
        const bool sized = ! content->getLocalBounds().isEmpty();
        const int w = sized ? content->getWidth() : area.getWidth();
        const int h = sized ? content->getHeight() : area.getHeight();
        content->setBounds (area.getX(), area.getY(),
                            juce::jmin (w, area.getWidth()), juce::jmin (h, area.getHeight()));
    }
}

void Frame::paint (juce::Graphics& g)
{
    uv::ground::paint (g, getLocalBounds().toFloat());

    /* The list: a bg-100 panel down the left edge, its hairline on the
     * right. */
    const auto list = getLocalBounds().withWidth (listWidth).toFloat();
    g.setColour (c::bg100);
    g.fillRect (list);
    g.setColour (c::line100);
    g.fillRect (list.withLeft (list.getRight() - uv::tok::size::hairline));

    const auto& title = uv::tok::type::title;
    uv::type::draw (g, uv::type::cased (title, "NI UI Gallery"),
                    { (float) pad, (float) pad, (float) (listWidth - 2 * pad), title.lineHeight },
                    uv::type::font (title), c::inkMuted);

    const auto& hint = uv::tok::type::hint;
    uv::type::draw (g, juce::String (uv::tok::systemName) + " " + uv::tok::release,
                    { (float) pad, (float) pad + title.lineHeight, (float) (listWidth - 2 * pad), hint.lineHeight },
                    uv::type::font (hint), c::inkDim);

    /* A page that is itself Luminous glows on the ground. */
    ni::ui::paintChildLights (g, *this);

    if (rows.empty())
    {
        uv::type::draw (g, "No pages registered.",
                        { (float) pad, (float) (pad + 16 + 14 + (int) sp::space6),
                          (float) (listWidth - 2 * pad), hint.lineHeight },
                        uv::type::font (hint), c::inkMuted);
        return;
    }

    const auto& label = uv::tok::type::label;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        const auto r = row.bounds.toFloat();
        if (row.page < 0)
        {
            uv::type::draw (g, uv::type::cased (label, row.text), r, uv::type::font (label), c::inkMuted,
                            juce::Justification::bottomLeft);
            continue;
        }

        if ((int) i == hovered && row.page != selected)
        {
            g.setColour (c::bg300);
            g.fillRect (r);
        }

        /* "There is no separate selected colour: selected is uv." */
        uv::type::draw (g, row.text, r.withTrimmedLeft ((float) pad), uv::type::value(),
                        row.page == selected ? c::uv : c::ink);
    }
}

int Frame::rowAt (juce::Point<int> p) const
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].page >= 0 && rows[i].bounds.contains (p))
            return (int) i;
    return -1;
}

void Frame::mouseDown (const juce::MouseEvent& e)
{
    const int row = rowAt (e.getPosition());
    if (row >= 0)
        show (rows[(size_t) row].page);
}

void Frame::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.getPosition());
    if (row != hovered)
    {
        hovered = row;
        repaint (0, 0, listWidth, height);
    }
}

void Frame::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint (0, 0, listWidth, height);
}

bool Frame::keyPressed (const juce::KeyPress& key)
{
    if (numPages() == 0)
        return false;
    if (key == juce::KeyPress::downKey)
    {
        show (juce::jmin (numPages() - 1, selected + 1));
        return true;
    }
    if (key == juce::KeyPress::upKey)
    {
        show (juce::jmax (0, selected - 1));
        return true;
    }
    return false;
}

} // namespace ni::ui::gallery

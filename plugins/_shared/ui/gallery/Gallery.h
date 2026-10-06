// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The gallery: the kit's components in their states, one page each, for a
 * person to look at and for a snapshot to hold.
 *
 * A PAGE REGISTERS ITSELF. One file in gallery/pages/ per component, and in it
 *
 *   NI_GALLERY_PAGE ("Controls", "Knob", []
 *   {
 *       return std::make_unique<KnobPage>();   // sized in its own constructor
 *   });
 *
 * -- nothing shared is edited, so components written at the same time cannot
 * collide over a list. The groups the system's components fall into are
 * "Foundation", "Controls" and "Display"; a page under any other group sorts
 * after them.
 *
 * A PAGE IS A SCENE: its component shows each state side by side (rest,
 * hover, pressed, on, disabled, focused ...), sized by itself, transparent
 * where the window ground should show. The Frame puts it on the ground; a
 * snapshot test renders the same page alone.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace ni::ui::gallery
{

struct Page
{
    juce::String group;
    juce::String name;
    std::function<std::unique_ptr<juce::Component>()> make;
};

/* Registers a page; true, so it can initialise a static. */
bool add (Page);

/* Every page, Foundation first, then Controls, Display and the rest by name;
 * within a group by name. */
std::vector<Page> pages();

/* The id of a page in --page and --shots: "<group>-<name>", lower case,
 * spaces as dashes. */
juce::String idOf (const Page&);

/*
 * The gallery window's content: the list of pages on the left, the page on
 * the ground to its right. 960 x 640, the size the snapshot of an empty frame
 * pins.
 */
class Frame final : public juce::Component
{
public:
    static constexpr int width = 960;
    static constexpr int height = 640;
    static constexpr int listWidth = 216;

    explicit Frame (std::vector<Page> pages);
    ~Frame() override;

    int numPages() const noexcept { return (int) pages.size(); }
    int current() const noexcept { return selected; }

    /* Shows page `index`, or none for -1. */
    void show (int index);
    /* Shows the page with this id or name; false if there is none. */
    bool show (const juce::String& idOrName);

    /* The page shown, or nullptr. */
    juce::Component* page() const noexcept { return content.get(); }
    /* Where a page sits: right of the list, inside the window padding. */
    juce::Rectangle<int> pageArea() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct Row
    {
        int page = -1;            // -1: a group heading
        juce::String text;
        juce::Rectangle<int> bounds;
    };

    void layoutRows();
    int rowAt (juce::Point<int>) const;

    std::vector<Page> pages;
    std::vector<Row> rows;
    std::unique_ptr<juce::Component> content;
    int selected = -1;
    int hovered = -1;

    JUCE_DECLARE_NON_COPYABLE (Frame)
};

} // namespace ni::ui::gallery

#define NI_GALLERY_CONCAT_(a, b) a##b
#define NI_GALLERY_CONCAT(a, b) NI_GALLERY_CONCAT_ (a, b)

/* NI_GALLERY_PAGE ("Group", "Name", factory): registers a page at start-up. */
#define NI_GALLERY_PAGE(group, name, ...)                                              \
    static const bool NI_GALLERY_CONCAT (niGalleryPage_, __LINE__) [[maybe_unused]] =  \
        ::ni::ui::gallery::add (::ni::ui::gallery::Page { group, name, __VA_ARGS__ })

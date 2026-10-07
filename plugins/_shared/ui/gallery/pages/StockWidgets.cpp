// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * JUCE's own widgets under uv::LookAndFeel, in each of their states: what any
 * stock widget in a window looks like without a component of the kit's own.
 * The states a pointer would make -- hover, pressed -- are forced, so the
 * page shows them side by side and a snapshot can hold them.
 */
#include "Gallery.h"

#include "Focus.h"
#include "UvLookAndFeel.h"
#include "UvTokens.h"
#include "UvType.h"

namespace
{
namespace c = uv::tok::colour;

/* A TextButton drawn in a state the pointer would give it. */
struct ForcedButton final : public juce::TextButton
{
    ForcedButton (const juce::String& text, bool hover, bool down)
        : juce::TextButton (text), forceHover (hover), forceDown (down) {}

    void paintButton (juce::Graphics& g, bool, bool) override
    {
        juce::TextButton::paintButton (g, forceHover, forceDown);
    }

    bool forceHover, forceDown;
};

/* The open list, as the LookAndFeel draws it when a select opens: a popup is
 * a window of its own, so the page draws its rows in place instead. */
struct OpenList final : public juce::Component
{
    OpenList() { setSize (160, 4 * 28 + 2); }

    void paint (juce::Graphics& g) override
    {
        auto& lnf = getLookAndFeel();
        lnf.drawPopupMenuBackground (g, getWidth(), getHeight());
        const char* items[] = { "1/8", "1/16", "1/16T", "1/32" };
        for (int i = 0; i < 4; ++i)
            lnf.drawPopupMenuItem (g, { 1, 1 + i * 28, getWidth() - 2, 28 }, false, true, i == 2, i == 1,
                                   false, items[i], {}, nullptr, nullptr);
    }
};

struct StockWidgetsPage final : public juce::Component
{
    StockWidgetsPage()
    {
        setSize (680, 470);

        /* ---- buttons: rest, hover, pressed, on, primary, disabled */
        const char* names[] = { "Copy", "Hover", "Pressed", "On", "Random", "Paste" };
        for (int i = 0; i < 6; ++i)
        {
            auto& b = *buttons.add (new ForcedButton (names[i], i == 1, i == 2));
            b.setClickingTogglesState (false);
            b.setToggleState (i == 3, juce::dontSendNotification);
            b.getProperties().set ("ni.primary", i == 4);
            b.setEnabled (i != 5);
            b.setBounds (i * 104, 24, 96, (int) uv::tok::size::controlH);
            addAndMakeVisible (b);
        }

        /* ---- switches: off, on, disabled, keyboard focus */
        for (int i = 0; i < 4; ++i)
        {
            auto& t = *toggles.add (new juce::ToggleButton (i == 3 ? "Focused" : "Join"));
            t.setToggleState (i == 1 || i == 3, juce::dontSendNotification);
            t.setEnabled (i != 2);
            if (i == 3)
                t.getProperties().set (ni::ui::focusVisibleProperty, true);
            t.setBounds (i * 156, 96, 140, (int) uv::tok::size::controlH);
            addAndMakeVisible (t);
        }

        /* ---- selects: rest, keyboard focus, disabled; and an open list */
        for (int i = 0; i < 3; ++i)
        {
            auto& s = *selects.add (new juce::ComboBox());
            s.addItemList ({ "1/8", "1/16", "1/16T", "1/32" }, 1);
            s.setSelectedId (2, juce::dontSendNotification);
            s.setEnabled (i != 2);
            if (i == 1)
                s.getProperties().set (ni::ui::focusVisibleProperty, true);
            s.setBounds (i * 120, 168, 104, (int) uv::tok::size::controlH);
            addAndMakeVisible (s);
        }
        addAndMakeVisible (list);
        list.setTopLeftPosition (376, 168);

        /* ---- knobs on a rotary slider: a value, a bipolar value, the
         * large one, disabled */
        const double values[] = { 0.32, 0.75, 0.6, 0.6 };
        for (int i = 0; i < 4; ++i)
        {
            auto& k = *knobs.add (new juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow));
            k.setRange (0.0, 1.0);
            k.setValue (values[i], juce::dontSendNotification);
            /* The card's 270 degrees, the gap at the bottom. */
            k.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                   juce::MathConstants<float>::pi * 2.75f, true);
            k.textFromValueFunction = [i] (double v)
            {
                return i == 1 ? juce::String ((v - 0.5) * 200.0, 0) + " %"
                              : juce::String (v * 100.0, 1) + " ms";
            };
            k.getProperties().set ("ni.bipolar", i == 1);
            k.getProperties().set ("ni.large", i == 2);
            k.setEnabled (i != 3);
            const int size = i == 2 ? (int) uv::tok::size::knobLg : (int) uv::tok::size::knob;
            k.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, (int) uv::tok::size::controlH);
            k.updateText();
            k.setBounds (i * 96, 320 - (size - 48), 64, size + 8 + (int) uv::tok::size::controlH);
            addAndMakeVisible (k);
        }

        /* ---- text: a name typed, an empty field with its placeholder. The
         * font first: a TextEditor's font applies to text typed after it. */
        for (auto* e : { &name, &empty })
        {
            e->setFont (uv::type::value());
            e->setIndents ((int) uv::tok::space::space2, 0);
            e->setJustification (juce::Justification::centredLeft);
        }
        name.setText ("Bus 1", juce::dontSendNotification);
        name.setBounds (400, 320, 120, (int) uv::tok::size::controlH);
        addAndMakeVisible (name);
        empty.setTextToShowWhenEmpty ("name this bus", c::inkMuted);
        empty.setBounds (536, 320, 120, (int) uv::tok::size::controlH);
        addAndMakeVisible (empty);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& style = uv::tok::type::label;
        const auto heading = [&] (const char* text, int y)
        {
            uv::type::draw (g, uv::type::cased (style, text), { 0.0f, (float) y, 400.0f, style.lineHeight },
                            uv::type::font (style), c::inkMuted);
        };
        heading ("TextButton", 0);
        heading ("ToggleButton", 72);
        heading ("ComboBox and its list", 144);
        heading ("Slider and TextEditor", 296);

        /* A stock widget is not Luminous: its light stops at its bounds, as
         * the focused select's does here. The kit's own controls glow past
         * theirs (Luminous.h). */
        uv::type::draw (g, "A stock widget's light stops at its bounds; the kit's controls are Luminous.",
                        { 0.0f, 440.0f, 680.0f, 14.0f }, uv::type::hint(), c::inkDim);
    }

    juce::OwnedArray<ForcedButton> buttons;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::ComboBox> selects;
    juce::OwnedArray<juce::Slider> knobs;
    OpenList list;
    juce::TextEditor name, empty;
};

} // namespace

NI_GALLERY_PAGE ("Foundation", "Stock widgets", [] { return std::make_unique<StockWidgetsPage>(); });

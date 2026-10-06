// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Button in each of its states, in both of its shapes, and the window
 * verbs as the Actions card groups them: joined icons and the labelled stack.
 */
#include "Gallery.h"

#include "Button.h"
#include "ControlsPage.h"
#include "Info.h"
#include "Pointer.h"

namespace
{
using ni::ui::Button;
using ni::ui::ButtonGroup;
using ni::ui::InfoText;

constexpr InfoText copyInfo { "Copy slot — put this slot, pattern and sound, on the clipboard." };
constexpr InfoText pasteInfo { "Paste slot — replace this slot with the one on the clipboard." };
constexpr InfoText exportInfo { "Export slot — save this slot to a file." };
constexpr InfoText exportAllInfo { "Export all — save every slot, the bank, to one file." };
constexpr InfoText importInfo { "Import — load a slot or a bank from a file; the file decides." };
constexpr InfoText orderInfo { "Order — click the steps in the order the fade brings them in." };
constexpr InfoText randomInfo { "Random — a new pattern for this slot." };
constexpr InfoText pauseInfo { "Pause — hold the picture; click again to let it run." };

struct Verb
{
    const char* icon;
    const char* text;
    const char* title;
    const InfoText* info;
};

const Verb verbs[] = {
    { "copy", "Copy slot", "Copy slot", &copyInfo },
    { "paste", "Paste slot", "Paste slot", &pasteInfo },
    { "export", "Export slot", "Export slot", &exportInfo },
    { "export-all", "Export all", "Export all", &exportAllInfo },
    { "import", "Import", "Import", &importInfo },
};

struct ButtonPage final : public ni::ui::gallery::ControlsPage
{
    ButtonPage()
    {
        ni::ui::gallery::Pointer pointer;

        /* ---- a word, led by a glyph or not: rest, hover, pressed, on;
         * then primary, disabled and focused */
        heading ("Button: rest, hover, pressed, on", 0, 0);
        heading ("primary, disabled, focus", 0, 72);
        const char* words[] = { "Copy slot", "Paste slot", "Shuffle", "ORDER 2/5", "Random", "Import", "Export all" };
        const char* glyphs[] = { "copy", "paste", "", "", "", "import", "export-all" };
        int x = 0;
        for (int i = 0; i < 7; ++i)
        {
            if (i == 4)
                x = 0;
            auto& b = *text.add (new Button (words[i], glyphs[i]));
            ni::ui::setInfo (b, i == 3 ? orderInfo.str() : randomInfo.str());
            b.setBounds (x, i < 4 ? 24 : 96, b.idealWidth(), (int) uv::tok::size::controlH);
            addAndMakeVisible (b);
            x += b.idealWidth() + 16;
        }
        pointer.enter (*text[1]);
        pointer.down (*text[2], { 4.0f, 4.0f });
        text[3]->setOn (true);
        text[4]->setPrimary (true);
        text[5]->setEnabled (false);
        showFocus (*text[6]);

        /* ---- the 28px square: rest, hover, pressed, on, disabled, focused */
        heading ("Icon: rest, hover, pressed, on, disabled, focus", 0, 144);
        const char* icons[] = { "copy", "paste", "export", "pause", "import", "loop" };
        for (int i = 0; i < 6; ++i)
        {
            auto& b = *square.add (new Button ({}, icons[i]));
            b.setTitle (icons[i]);
            ni::ui::setInfo (b, pauseInfo);
            b.setBounds (i * 48, 168, b.idealWidth(), (int) uv::tok::size::controlH);
            addAndMakeVisible (b);
        }
        ni::ui::gallery::Pointer second;
        second.enter (*square[1]);
        second.down (*square[2], { 4.0f, 4.0f });
        square[3]->setOn (true);
        square[4]->setEnabled (false);
        showFocus (*square[5]);

        /* ---- the window verbs, joined: copy, paste, export, export all,
         * import, sharing hairlines; the pointer over paste raises it */
        heading ("Actions, joined", 0, 216);
        for (const auto& v : verbs)
        {
            auto& b = *joinedButtons.add (new Button ({}, v.icon));
            b.setTitle (v.title);
            ni::ui::setInfo (b, *v.info);
            joined.add (b);
        }
        joined.setBounds (joined.idealSize().withPosition (0, 240));
        addAndMakeVisible (joined);
        ni::ui::gallery::Pointer third;
        third.enter (*joinedButtons[1]);

        /* ... and focused: export's ring falls over its neighbours. */
        for (const auto& v : verbs)
        {
            auto& b = *focusedButtons.add (new Button ({}, v.icon));
            b.setTitle (v.title);
            ni::ui::setInfo (b, *v.info);
            focused.add (b);
        }
        focused.setBounds (focused.idealSize().withPosition (0, 296));
        addAndMakeVisible (focused);
        showFocus (*focusedButtons[2]);
        note ("136px for five; hover and focus raise one over its neighbours", 0, 336);

        /* ---- the labelled stack, as wide as its widest */
        heading ("Actions, stack", 440, 216);
        for (const auto& v : verbs)
        {
            auto& b = *stackButtons.add (new Button (v.text, v.icon));
            ni::ui::setInfo (b, *v.info);
            stack.add (b);
        }
        stack.setBounds (stack.idealSize().withPosition (440, 240));
        addAndMakeVisible (stack);

        setSize (680, 420);
    }

    juce::OwnedArray<Button> text, square, joinedButtons, focusedButtons, stackButtons;
    ButtonGroup joined { ButtonGroup::Form::joined };
    ButtonGroup focused { ButtonGroup::Form::joined };
    ButtonGroup stack { ButtonGroup::Form::stack };
};

} // namespace

NI_GALLERY_PAGE ("Controls", "Button", [] { return std::make_unique<ButtonPage>(); });

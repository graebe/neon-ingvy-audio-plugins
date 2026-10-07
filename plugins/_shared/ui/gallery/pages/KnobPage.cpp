// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Knob in each of its states, as its card shows them: at the minimum, part
 * way, at the maximum, with detents (one of them lit, the value on it),
 * disabled, and with the keyboard's focus round the dial -- each with the
 * plugin's text in its readout, the unit muted.
 */
#include "Gallery.h"

#include "ControlsPage.h"
#include "Info.h"
#include "Knob.h"

namespace
{
using ni::ui::InfoText;
using ni::ui::Knob;

constexpr InfoText lengthInfo { "Length — 1 to 128 steps; holds on bar lengths, Page Up/Down jumps." };
constexpr InfoText typedInfo { "Length value — click to type one; Enter sets it, Escape cancels." };

struct KnobPage final : public ni::ui::gallery::ControlsPage
{
    KnobPage()
    {
        heading ("Knob: minimum, part way, maximum, on a detent, disabled, focus", 0, 0);

        struct State
        {
            const char* label;
            float value;
            const char* text;
        };
        const State states[] = {
            { "Amount", 0.0f, "0.00 %" },
            { "Attack", 0.3f, "12.5 ms" },
            { "Width", 1.0f, "100.00 %" },
            { "Length", 15.0f / 127.0f, "16" },
            { "Decay", 0.5f, "40.0 ms" },
            { "Rate", 7.0f / 12.0f, "1/16" },
        };

        int x = 0;
        for (int i = 0; i < 6; ++i)
        {
            auto& k = *row.add (new Knob (states[i].label));
            k.setValue (states[i].value);
            k.setValueText (states[i].text);
            ni::ui::setInfo (k, lengthInfo);
            k.setReadoutInfo (typedInfo);
            k.setBounds (x, 24, 96, Knob::cardHeight);
            addAndMakeVisible (k);
            x += 96 + 16;
        }

        /* Length at 1/16 in 4/4: half a bar, one, two and four, the value on
         * the second. */
        row[3]->setDetents ({ 7.0 / 127.0, 15.0 / 127.0, 31.0 / 127.0, 63.0 / 127.0 });
        row[4]->setEnabled (false);
        showFocus (row[5]->dial());

        note ("drag 200px for the range, shift five times finer; a detent's tick is lit while the value is on it",
              0, 24 + Knob::cardHeight + 12);

        setSize (680, 24 + Knob::cardHeight + 36);
    }

    juce::OwnedArray<Knob> row;
};

} // namespace

NI_GALLERY_PAGE ("Controls", "Knob", [] { return std::make_unique<KnobPage>(); });

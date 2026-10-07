// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Readout in each of its states, as its card shows them: a value with its
 * unit, a unitless value, one being typed into, one disabled -- and, beside
 * them, the different thing the card warns about, the big `readout` text
 * style with no box.
 *
 * THE OPEN FIELD IS REALLY OPEN: the page calls showEditor(), as Enter on a
 * knob would, so the picture is the field JUCE draws and not a drawing of it.
 * A field that is open is modal (it is how a click elsewhere commits it), so
 * in the gallery the first click elsewhere closes it.
 */
#include "DisplayPage.h"
#include "Gallery.h"

#include "Readout.h"

namespace
{

namespace c = uv::tok::colour;

struct ReadoutPage final : public ni::ui::gallery::DisplayPage
{
    static constexpr int gap = 16, w = ni::ui::Readout::minWidth, top = 24;

    ReadoutPage()
    {
        setSize (560, 64);

        place (rest, "rest", 0, "32 ms");
        place (unitless, "unitless", 1, "1.00");
        place (editing, "editing", 2, "72 ms");
        place (disabled, "disabled", 3, "0 ms");
        disabled.setEnabled (false);

        caption ("readout style", { (float) (4 * (w + gap)), 0.0f, 200.0f, 14.0f });

        editing.showEditor();
    }

    void place (ni::ui::Readout& r, const juce::String& name, int slot, const juce::String& text)
    {
        const int x = slot * (w + gap);
        caption (name, { (float) x, 0.0f, (float) w + 8.0f, 14.0f });
        r.setValueText (text);
        addAndMakeVisible (r);
        r.setBounds (x, top, w, ni::ui::Readout::height);
    }

    void paintOver (juce::Graphics& g) override
    {
        /* .ph-readout-text: 28/32, weight 500, ink; and in ink-muted. */
        const auto& style = uv::tok::type::readout;
        const float x = (float) (4 * (w + gap));
        const float y = (float) top + (ni::ui::Readout::height - style.lineHeight) * 0.5f;
        uv::type::draw (g, "18", { x, y, 48.0f, style.lineHeight }, uv::type::readout(), c::ink);
        uv::type::draw (g, "127.00", { x + 64.0f, y, 120.0f, style.lineHeight }, uv::type::readout(), c::inkMuted);
    }

    ni::ui::Readout rest, unitless, editing, disabled;
};

} // namespace

NI_GALLERY_PAGE ("Display", "Readout", [] { return std::make_unique<ReadoutPage>(); });

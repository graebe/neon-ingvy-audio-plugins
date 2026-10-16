// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Filter Designer's node row. NodeRow.h says what it shows.
 */
#include "NodeRow.h"

#include "Info.h"
#include "InfoLines.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

namespace ni::fd
{

namespace
{
namespace c = uv::tok::colour;

/* The glyphs as the board draws them, in 16px boxes. */
const char* glyphSvg (int t)
{
    switch (t)
    {
        case type::bell:      return "M1 12H4.5C6.5 12 6.5 4 8 4S9.5 12 11.5 12H15";
        case type::lowShelf:  return "M1 5H5L9 11H15";
        case type::highShelf: return "M1 11H7L11 5H15";
        case type::highPass:    return "M2 14C4 7 5 5 8.5 5H15";
        case type::lowPass:   return "M1 5H7.5C11 5 12 7 14 14";
        case type::bandPass:  return "M1 13C5 13 6 4 8 4S11 13 15 13";
        case type::notch:     return "M1 5H6L8 13L10 5H15";
        case type::allPass:   return "M1 5H15M2 11C4 8 6 8 8 11S12 14 14 11";
        case type::tilt:      return "M1 12L15 4";
        default:              return "";
    }
}

constexpr const char* isoGlyph = "M1 12H5V6H11V9H15";
} // namespace

juce::Path typeGlyph (int t)
{
    return juce::Drawable::parseSVGPath (glyphSvg (t));
}

class NodeRow::Cell final : public juce::Button
{
public:
    Cell (NodeRow& r, int i) : juce::Button ("Filter " + juce::String (i + 1)), owner (r), index (i)
    {
        ni::ui::setInfo (*this, i == param::nodes ? info::isoCell : info::cell);
        setWantsKeyboardFocus (true);
    }

    void read (Model& m, bool isSelected)
    {
        const auto value = [&m] (int p) { auto& q = m.parameter (p); return (double) q.convertFrom0to1 (q.getValue()); };
        int t = type::empty;
        bool on = false;
        if (index == param::nodes)
            on = value (param::isoOn) >= 0.5;
        else
        {
            t = (int) value (param::node (index, param::type));
            on = value (param::node (index, param::on)) >= 0.5;
        }
        if (t == kind && on == playing && isSelected == chosen)
            return;
        kind = t;
        playing = on;
        chosen = isSelected;
        static const char* const names[] = { "empty", "Bell", "Low Shelf", "High Shelf", "High-Pass",
                                             "Low-Pass", "Band-Pass", "Notch", "All-Pass", "Tilt" };
        setTitle (index == param::nodes ? juce::String ("Isolator") + (on ? "" : ", off")
                                        : "Filter " + juce::String (index + 1) + ", " + names[kind]
                                              + (kind != type::empty && ! on ? ", bypassed" : ""));
        repaint();
    }

    void paintButton (juce::Graphics& g, bool hover, bool) override
    {
        const auto box = getLocalBounds().toFloat();
        const bool iso = index == param::nodes;
        const bool empty = ! iso && kind == type::empty;
        g.setColour (hover ? c::bg300 : c::bg200);
        g.fillRect (box);
        if (chosen)
            uv::light::glowFocus (g, box);
        g.setColour (chosen ? c::uv : empty || (iso && ! playing) ? c::line100 : c::line200);
        g.drawRect (box, 1.0f);
        g.setFont (uv::type::hint());
        g.setColour (empty || (iso && ! playing) ? c::inkDim : c::inkMuted);
        g.drawText (iso ? "ISO" : juce::String (index + 1), juce::Rectangle<float> (4.0f, 2.0f, 30.0f, 14.0f),
                    juce::Justification::centredLeft);
        if (empty)
            return;
        auto glyph = juce::Drawable::parseSVGPath (iso ? isoGlyph : glyphSvg (kind));
        glyph.applyTransform (juce::AffineTransform::translation (11.0f, 17.0f));
        g.setColour (playing ? c::uv : c::inkDim);
        g.strokePath (glyph, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
    }

    void clicked (const juce::ModifierKeys& mods) override
    {
        if (mods.isAltDown())
        {
            auto& m = owner.model;
            const int on = index == param::nodes ? (int) param::isoOn : param::node (index, param::on);
            if (index != param::nodes && kind == type::empty)
                return;
            auto& p = m.parameter (on);
            p.beginChangeGesture();
            p.setValueNotifyingHost (playing ? 0.0f : 1.0f);
            p.endChangeGesture();
            return;
        }
        if (owner.onSelect)
            owner.onSelect (index);
    }

private:
    NodeRow& owner;
    const int index;
    int kind = -1;
    bool playing = false, chosen = false;
};

NodeRow::NodeRow (Model& m) : model (m)
{
    for (int i = 0; i < cells; ++i)
    {
        row[(size_t) i] = std::make_unique<Cell> (*this, i);
        addAndMakeVisible (*row[(size_t) i]);
    }
    update();
}

NodeRow::~NodeRow() = default;

juce::Button& NodeRow::cellAt (int index)
{
    return *row[(size_t) index];
}

void NodeRow::setSelected (int sel)
{
    selection = sel;
    update();
}

void NodeRow::update()
{
    for (int i = 0; i < cells; ++i)
        row[(size_t) i]->read (model, i == selection);
}

void NodeRow::resized()
{
    for (int i = 0; i < param::nodes; ++i)
        row[(size_t) i]->setBounds (i * pitch, 0, cell, cell);
    row[(size_t) param::nodes]->setBounds (getWidth() - cell, 0, cell, cell);
}

} // namespace ni::fd

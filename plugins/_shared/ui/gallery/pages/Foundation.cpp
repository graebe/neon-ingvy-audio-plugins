// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The design layer, as pages: every colour token, the six text styles, the
 * fifteen icons in the colours a control gives them, and the three lights.
 * What a re-synced design changes shows here first, and the snapshot of each
 * page says by how much.
 */
#include "Gallery.h"

#include "Luminous.h"
#include "UvGround.h"
#include "UvIcons.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

namespace
{
namespace c = uv::tok::colour;
namespace sp = uv::tok::space;

/* A page's own small heading: the label style, ink-muted. */
void heading (juce::Graphics& g, const juce::String& text, float x, float y)
{
    const auto& style = uv::tok::type::label;
    uv::type::draw (g, uv::type::cased (style, text), { x, y, 400.0f, style.lineHeight },
                    uv::type::font (style), c::inkMuted);
}

/* ================================================================ colour == */

/* A colour as tokens.json writes it: #rrggbb, with its alpha last when it has
 * one (#rrggbbaa), where JUCE would put it first. */
juce::String css (juce::Colour colour)
{
    auto text = "#" + colour.toDisplayString (false).toLowerCase();
    if (colour.getAlpha() < 255)
        text << juce::String::toHexString ((int) colour.getAlpha()).paddedLeft ('0', 2);
    return text;
}

struct ColourPage final : public juce::Component
{
    struct Swatch
    {
        const char* name;
        juce::Colour colour;
    };

    std::vector<Swatch> swatches {
        { "bg-000", c::bg000 }, { "bg-dot", c::bgDot }, { "bg-100", c::bg100 }, { "bg-200", c::bg200 },
        { "bg-300", c::bg300 }, { "line-100", c::line100 }, { "line-200", c::line200 }, { "ink", c::ink },
        { "ink-muted", c::inkMuted }, { "ink-dim", c::inkDim }, { "uv", c::uv }, { "uv-deep", c::uvDeep },
        { "uv-glow", c::uvGlow }, { "on-uv", c::onUv }, { "amber", c::amber }, { "red", c::red },
        { "spec-0", c::spec0 }, { "spec-1", c::spec1 }, { "spec-2", c::spec2 }, { "spec-3", c::spec3 },
        { "spec-4", c::spec4 }, { "plot-fill", c::plotFill }, { "plot-ghost", c::plotGhost }, { "plot-dry", c::plotDry },
    };

    static constexpr int cols = 6, cell = 104, chip = 40;

    ColourPage() { setSize (cols * cell, 4 * (chip + 44) + 24); }

    void paint (juce::Graphics& g) override
    {
        heading (g, "Colour", 0.0f, 0.0f);
        for (size_t i = 0; i < swatches.size(); ++i)
        {
            const float x = (float) ((int) i % cols * cell);
            const float y = 24.0f + (float) ((int) i / cols * (chip + 44));
            const juce::Rectangle<float> r (x, y, (float) chip, (float) chip);

            g.setColour (swatches[i].colour);
            g.fillRect (r);
            g.setColour (c::line200);
            g.drawRect (r, uv::tok::stroke::strokeHair);

            uv::type::draw (g, swatches[i].name, { x, y + chip + 4.0f, (float) cell - 8.0f, 14.0f },
                            uv::type::hint(), c::ink);
            uv::type::draw (g, css (swatches[i].colour), { x, y + chip + 18.0f, (float) cell - 8.0f, 14.0f },
                            uv::type::hint(), c::inkMuted);
        }
    }
};

/* ================================================================== type == */

struct TypePage final : public juce::Component
{
    TypePage() { setSize (640, 330); }

    void paint (juce::Graphics& g) override
    {
        heading (g, "Type", 0.0f, 0.0f);

        struct Row
        {
            const uv::tok::TextStyle& style;
            const char* name;
            const char* sample;
            juce::Colour colour;
        };
        const Row rows[] = {
            { uv::tok::type::readout, "readout 28/32 500", "127.00", c::ink },
            { uv::tok::type::title,   "title 12/16 500 .08em", "Trance Gate", c::inkMuted },
            { uv::tok::type::label,   "label 11/14 500 .1em", "Attack", c::inkMuted },
            { uv::tok::type::value,   "value 13/16 400", "32 ms", c::ink },
            { uv::tok::type::button,  "button 12/16 500 .04em", "Copy patch", c::ink },
            { uv::tok::type::hint,    "hint 10/14 400 .02em", "click a step to toggle", c::inkMuted },
        };

        float y = 28.0f;
        for (const auto& row : rows)
        {
            const float h = juce::jmax (row.style.lineHeight, 28.0f);
            uv::type::draw (g, row.name, { 0.0f, y, 200.0f, h }, uv::type::hint(), c::inkDim);
            uv::type::draw (g, uv::type::cased (row.style, row.sample), { 216.0f, y, 420.0f, h },
                            uv::type::font (row.style), row.colour);
            y += h + 12.0f;
        }

        /* A value with its unit, and the characters the editors' lines use. */
        uv::type::drawValueWithUnit (g, "40.0 ms", { 216.0f, y, 96.0f, 28.0f });
        uv::type::drawValueWithUnit (g, "1/16", { 320.0f, y, 64.0f, 28.0f });
        uv::type::drawValueWithUnit (g, "-6.0 dB", { 392.0f, y, 96.0f, 28.0f }, false);
        uv::type::draw (g, juce::String::fromUTF8 ("\xe2\x80\x94 \xe2\x80\x93 \xe2\x88\x92 \xc2\xb7 \xe2\x86\x91\xe2\x86\x93 \xe2\x80\xa6 \xc2\xb5s \xc3\xa4\xc3\xb6\xc3\xbc \xc2\xbd \xe2\x8c\x98"),
                        { 0.0f, y, 200.0f, 28.0f }, uv::type::value(), c::ink);
    }
};

/* ================================================================= icons == */

/*
 * Each glyph in the four colours a control gives it (Icon README): ink on a
 * resting bg-200 well, on-uv on a lit uv button, ink-dim disabled on bg-100,
 * and -- the record button's own -- bg-000 on red.
 */
struct IconPage final : public juce::Component
{
    static constexpr int size = 28, gap = 8;

    IconPage()
    {
        const int n = uv::iconNames().size();
        setSize (120 + n * (size + gap), 24 + 4 * (size + gap) + 20);
    }

    void paint (juce::Graphics& g) override
    {
        heading (g, "Icons", 0.0f, 0.0f);

        struct Tint
        {
            const char* name;
            juce::Colour fill, edge, glyph;
        };
        const Tint tints[] = {
            { "rest", c::bg200, c::line200, c::ink },
            { "on", c::uv, c::uv, c::onUv },
            { "disabled", c::bg100, c::line100, c::inkDim },
            { "record on", c::red, c::red, c::bg000 },
        };

        const auto& names = uv::iconNames();
        for (int t = 0; t < 4; ++t)
        {
            const float y = 24.0f + (float) (t * (size + gap));
            uv::type::draw (g, tints[t].name, { 0.0f, y, 112.0f, (float) size }, uv::type::hint(), c::inkMuted);

            for (int i = 0; i < names.size(); ++i)
            {
                const juce::Rectangle<float> r ((float) (120 + i * (size + gap)), y, (float) size, (float) size);
                g.setColour (tints[t].fill);
                g.fillRect (r);
                g.setColour (tints[t].edge);
                g.drawRect (r, uv::tok::stroke::strokeHair);
                uv::drawIcon (g, names[i], tints[t].glyph, r);
            }
        }

        const float y = 24.0f + 4.0f * (float) (size + gap);
        for (int i = 0; i < names.size(); ++i)
            uv::type::draw (g, names[i], { (float) (120 + i * (size + gap)) - 4.0f, y, (float) size + 8.0f, 14.0f },
                            uv::type::font (uv::tok::type::hint).withPointHeight (7.0f), c::inkDim,
                            juce::Justification::centred);
    }
};

/* ================================================================= light == */

/* A lit primary button, glowing past its own bounds: its light is painted
 * by the page it sits on (Luminous.h). */
struct LitButton final : public juce::Component, public ni::ui::Luminous
{
    LitButton() { setSize (96, (int) uv::tok::size::controlH); }

    void paintLight (juce::Graphics& g) override
    {
        uv::light::glowLed (g, getLocalBounds().toFloat());
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (c::uv);
        uv::type::draw (g, "Random", getLocalBounds().toFloat(), uv::type::button(), c::onUv,
                        juce::Justification::centred);
    }
};

struct LightPage final : public juce::Component
{
    LightPage()
    {
        setSize (640, 180);
        addAndMakeVisible (lit);
        lit.setTopLeftPosition (520, 54);
    }

    LitButton lit;

    void paint (juce::Graphics& g) override
    {
        heading (g, "Light", 0.0f, 0.0f);

        /* glow-led: a lit step, an LED's lens, a playhead's step. */
        const juce::Rectangle<float> step (24.0f, 48.0f, 40.0f, 40.0f);
        uv::light::glowLed (g, step);
        g.setColour (c::uv);
        g.fillRect (step);

        juce::Path lens;
        lens.addEllipse (112.0f, 64.0f, 8.0f, 8.0f);
        uv::light::glowLed (g, lens);
        g.setColour (c::uv);
        g.fillPath (lens);

        /* glow-focus: around a 28px button and around a knob's disc. */
        const juce::Rectangle<float> button (168.0f, 54.0f, 96.0f, 28.0f);
        uv::light::glowFocus (g, button);
        g.setColour (c::bg200);
        g.fillRect (button);
        g.setColour (c::line200);
        g.drawRect (button, uv::tok::stroke::strokeHair);

        const juce::Rectangle<float> disc (312.0f, 52.0f, 32.0f, 32.0f);
        uv::light::glowFocus (g, disc, 16.0f);
        g.setColour (c::bg200);
        g.fillEllipse (disc);

        /* The arc glow, under a knob's value arc. */
        juce::Path arc;
        arc.addCentredArc (424.0f, 68.0f, 20.0f, 20.0f, 0.0f,
                           juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.5f, true);
        juce::Path stroke;
        juce::PathStrokeType (uv::tok::stroke::strokeRail, juce::PathStrokeType::mitered,
                              juce::PathStrokeType::butt).createStrokedPath (stroke, arc);
        uv::light::glowArc (g, stroke);
        g.setColour (c::uv);
        g.fillPath (stroke);

        const char* names[] = { "glow-led", "glow-led", "glow-focus", "glow-focus", "arc", "luminous" };
        const float xs[] = { 24.0f, 96.0f, 168.0f, 304.0f, 404.0f, 520.0f };
        for (int i = 0; i < 6; ++i)
            uv::type::draw (g, names[i], { xs[i], 112.0f, 96.0f, 14.0f }, uv::type::hint(), c::inkMuted);

        ni::ui::paintChildLights (g, *this);
    }
};

} // namespace

NI_GALLERY_PAGE ("Foundation", "Colour", [] { return std::make_unique<ColourPage>(); });
NI_GALLERY_PAGE ("Foundation", "Type", [] { return std::make_unique<TypePage>(); });
NI_GALLERY_PAGE ("Foundation", "Icons", [] { return std::make_unique<IconPage>(); });
NI_GALLERY_PAGE ("Foundation", "Light", [] { return std::make_unique<LightPage>(); });

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Ultraviolet's type: the embedded faces and the six styles. See UvType.h.
 */
#include "UvType.h"

#include <NiUiAssets.h>

namespace uv
{

namespace
{
/*
 * The faces, owned until JUCE shuts down. createSystemTypefaceFor copies the
 * data and registers the face with the platform for as long as a Ptr to it
 * lives, which is also what lets a font asked for by the name "JetBrains Mono"
 * find these files instead of an installed copy.
 */
class FaceStore final : private juce::DeletedAtShutdown
{
public:
    FaceStore()
    {
        faces.regular = juce::Typeface::createSystemTypefaceFor (
            ni_ui_assets::JetBrainsMonoRegular_ttf,
            (size_t) ni_ui_assets::JetBrainsMonoRegular_ttfSize);
        faces.medium = juce::Typeface::createSystemTypefaceFor (
            ni_ui_assets::JetBrainsMonoMedium_ttf,
            (size_t) ni_ui_assets::JetBrainsMonoMedium_ttfSize);

        /* Both are in this binary; a null here is a platform that refused a
         * valid TrueType file, and every glyph after it would be some other
         * face's. */
        jassert (faces.regular != nullptr && faces.medium != nullptr);
    }

    ~FaceStore() override { clearSingletonInstance(); }

    Fonts faces;

    JUCE_DECLARE_SINGLETON_INLINE (FaceStore, false)
};
} // namespace

juce::Typeface::Ptr Fonts::forWeight (int cssWeight) const
{
    return cssWeight >= 500 ? medium : regular;
}

const Fonts& fonts()
{
    JUCE_ASSERT_MESSAGE_THREAD
    return FaceStore::getInstance()->faces;
}

namespace type
{

juce::Font font (const tok::TextStyle& style)
{
    const auto face = fonts().forWeight (style.weight);
    juce::Font f (juce::FontOptions (face).withPointHeight (style.size));

    /* CSS letter-spacing is in em, which is the point height; JUCE's tracking
     * is a share of the JUCE height (ascent plus descent). */
    if (style.tracking != 0.0f && f.getHeight() > 0.0f)
        f = f.withExtraKerningFactor (style.tracking * style.size / f.getHeight());

    return f;
}

juce::Font readout() { return font (tok::type::readout); }
juce::Font title()   { return font (tok::type::title); }
juce::Font label()   { return font (tok::type::label); }
juce::Font value()   { return font (tok::type::value); }
juce::Font button()  { return font (tok::type::button); }
juce::Font hint()    { return font (tok::type::hint); }

bool isUpper (const tok::TextStyle& style)
{
    /* The styles are inline variables, one object each in the program, so
     * their addresses name them. */
    return &style == &tok::type::title || &style == &tok::type::label;
}

juce::String cased (const tok::TextStyle& style, const juce::String& text)
{
    return isUpper (style) ? text.toUpperCase() : text;
}

float width (const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (f, text);
}

void draw (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> box,
           const juce::Font& f, juce::Colour colour, juce::Justification just)
{
    if (text.isEmpty() || box.isEmpty())
        return;

    g.setFont (f);
    g.setColour (colour);
    g.drawText (text, box, just, true);
}

std::pair<juce::String, juce::String> splitUnit (const juce::String& text)
{
    const int cut = text.lastIndexOfChar (' ');
    if (cut <= 0)
        return { text, {} };
    return { text.substring (0, cut), text.substring (cut + 1) };
}

void drawValueWithUnit (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> box,
                        bool enabled)
{
    /* .readout .unit { margin-left: 2px } */
    constexpr float unitGap = 2.0f;

    const auto [number, unit] = splitUnit (text);
    const auto f = value();

    const float wNumber = width (f, number);
    const float wUnit = unit.isEmpty() ? 0.0f : unitGap + width (f, unit);
    const float x = box.getCentreX() - (wNumber + wUnit) * 0.5f;

    draw (g, number, { x, box.getY(), wNumber + unitGap, box.getHeight() }, f,
          enabled ? tok::colour::ink : tok::colour::inkDim);

    if (unit.isNotEmpty())
        draw (g, unit, { x + wNumber + unitGap, box.getY(), wUnit + unitGap, box.getHeight() }, f,
              enabled ? tok::colour::inkMuted : tok::colour::inkDim);
}

} // namespace type

} // namespace uv

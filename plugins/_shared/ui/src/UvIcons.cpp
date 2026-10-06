// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The fifteen glyphs. See UvIcons.h.
 */
#include "UvIcons.h"

#include <NiUiAssets.h>

#include <iterator>
#include <map>
#include <utility>

namespace uv
{

namespace
{
/* The README's own order. */
const char* const kNames[] = {
    "play", "pause", "stop", "record", "loop", "copy", "paste", "export",
    "export-all", "import", "shuffle", "reset", "link", "chevron", "power",
};

/* "play and record are filled; every other glyph is stroke only." */
bool filled (const juce::String& name)
{
    return name == "play" || name == "record";
}

/* The embedded file for a glyph, found by the name it had on disk. */
juce::String svgText (const juce::String& name)
{
    const auto file = name + ".svg";
    for (int i = 0; i < ni_ui_assets::namedResourceListSize; ++i)
    {
        if (file == ni_ui_assets::originalFilenames[i])
        {
            int size = 0;
            const auto* data = ni_ui_assets::getNamedResource (ni_ui_assets::namedResourceList[i], size);
            return juce::String::fromUTF8 (data, size);
        }
    }
    return {};
}

void tintTree (juce::Drawable& d, juce::Colour colour, bool fill)
{
    if (auto* shape = dynamic_cast<juce::DrawableShape*> (&d))
    {
        if (! shape->getStrokeFill().isInvisible())
            shape->setStrokeFill (colour);
        if (fill || ! shape->getFill().isInvisible())
            shape->setFill (colour);
    }

    if (auto* group = dynamic_cast<juce::DrawableComposite*> (&d))
        for (int i = 0; i < group->getNumChildren(); ++i)
            tintTree (group->getChild (i), colour, fill);
}

/*
 * Each file parsed once, and the coloured copies drawIcon() asks for. Owned
 * until JUCE shuts down and not after it, as everything the kit caches is
 * (UvType.h says why).
 */
class IconStore final : private juce::DeletedAtShutdown
{
public:
    IconStore()
    {
        for (const auto* n : kNames)
        {
            const juce::String name (n);
            if (auto glyph = juce::Drawable::createFromSVGString (svgText (name)))
                parsed.emplace (name, std::move (glyph));

            /* Every glyph the set lists is in the design mirror, and every
             * one parses: the files are part of this build. */
            jassert (parsed.count (name) == 1);
        }
    }

    ~IconStore() override { clearSingletonInstance(); }

    const juce::Drawable* find (const juce::String& name) const
    {
        const auto it = parsed.find (name);
        return it != parsed.end() ? it->second.get() : nullptr;
    }

    juce::Drawable* coloured (const juce::String& name, juce::Colour colour)
    {
        const auto key = std::make_pair (name, colour.getARGB());
        if (const auto it = cache.find (key); it != cache.end())
            return it->second.get();

        const auto* source = find (name);
        if (source == nullptr)
            return nullptr;

        /* A handful of colours per glyph is all a window uses; a cache that
         * grew past this would mean colours are being computed per frame. */
        if (cache.size() > 256)
            cache.clear();

        auto copy = source->createCopy();
        tintTree (*copy, colour, filled (name));
        return cache.emplace (key, std::move (copy)).first->second.get();
    }

    JUCE_DECLARE_SINGLETON_INLINE (IconStore, false)

private:
    std::map<juce::String, std::unique_ptr<juce::Drawable>> parsed;
    std::map<std::pair<juce::String, juce::uint32>, std::unique_ptr<juce::Drawable>> cache;
};
} // namespace

const juce::StringArray& iconNames()
{
    static const juce::StringArray names (kNames, (int) std::size (kNames));
    return names;
}

bool hasIcon (const juce::String& name)
{
    return iconNames().contains (name);
}

bool isFilledIcon (const juce::String& name)
{
    return hasIcon (name) && filled (name);
}

std::unique_ptr<juce::Drawable> icon (const juce::String& name, juce::Colour colour)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto* source = IconStore::getInstance()->find (name);
    jassert (source != nullptr);   // not one of the fifteen
    if (source == nullptr)
        return {};

    auto copy = source->createCopy();
    tintTree (*copy, colour, filled (name));
    return copy;
}

void tint (juce::Drawable& glyph, const juce::String& name, juce::Colour colour)
{
    tintTree (glyph, colour, filled (name));
}

void drawIcon (juce::Graphics& g, const juce::String& name, juce::Colour colour,
               juce::Rectangle<float> area)
{
    JUCE_ASSERT_MESSAGE_THREAD
    auto* glyph = IconStore::getInstance()->coloured (name, colour);
    jassert (glyph != nullptr);   // not one of the fifteen
    if (glyph == nullptr)
        return;

    /* The glyph's own 16px grid, centred: never scaled to the area, because
     * an icon is 16px inside its 28px control and never larger. */
    const auto origin = area.getCentre() - juce::Point<float> (8.0f, 8.0f);
    glyph->draw (g, 1.0f, juce::AffineTransform::translation (origin));
}

} // namespace uv

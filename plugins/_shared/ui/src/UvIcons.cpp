// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The fifteen glyphs, and the products' own. See UvIcons.h.
 */
#include "UvIcons.h"

#include <NiUiAssets.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

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

/* The system's files: the kit's own assets. */
const IconSource systemIcons { ni_ui_assets::namedResourceListSize, ni_ui_assets::namedResourceList,
                               ni_ui_assets::originalFilenames, ni_ui_assets::getNamedResource };

/* The products' sets, in the order they were added. */
std::vector<IconSource>& productIcons()
{
    static std::vector<IconSource> sources;
    return sources;
}

/* Where a source keeps the file for a glyph, found by the name it had on
 * disk; -1 if it has none. */
int indexIn (const IconSource& source, const juce::String& name)
{
    const auto file = name + ".svg";
    for (int i = 0; i < source.count; ++i)
        if (file == source.originalFilenames[i])
            return i;
    return -1;
}

juce::String svgText (const IconSource& source, int index)
{
    int size = 0;
    const auto* data = source.resource (source.resourceNames[index], size);
    return juce::String::fromUTF8 (data, size);
}

/* The product set that has `name`, or nullptr. */
const IconSource* productWith (const juce::String& name)
{
    for (const auto& source : productIcons())
        if (indexIn (source, name) >= 0)
            return &source;
    return nullptr;
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
            if (const int i = indexIn (systemIcons, name); i >= 0)
                if (auto glyph = juce::Drawable::createFromSVGString (svgText (systemIcons, i)))
                    parsed.emplace (name, std::move (glyph));

            /* Every glyph the set lists is in the design mirror, and every
             * one parses: the files are part of this build. */
            jassert (parsed.count (name) == 1);
        }
    }

    ~IconStore() override { clearSingletonInstance(); }

    /* The system's glyphs were parsed at the start; a product's is parsed
     * the first time it is asked for. */
    const juce::Drawable* find (const juce::String& name)
    {
        if (const auto it = parsed.find (name); it != parsed.end())
            return it->second.get();
        const auto* source = iconNames().contains (name) ? nullptr : productWith (name);
        if (source == nullptr)
            return nullptr;
        auto glyph = juce::Drawable::createFromSVGString (svgText (*source, indexIn (*source, name)));
        jassert (glyph != nullptr);   // a product's file that does not parse
        if (glyph == nullptr)
            return nullptr;
        return parsed.emplace (name, std::move (glyph)).first->second.get();
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
    return iconNames().contains (name) || productWith (name) != nullptr;
}

void addIcons (const IconSource& source)
{
    JUCE_ASSERT_MESSAGE_THREAD
    auto& sources = productIcons();
    const auto same = [&source] (const IconSource& s) { return s.resource == source.resource && s.resourceNames == source.resourceNames; };
    if (std::any_of (sources.begin(), sources.end(), same))
        return;

    /* The system's set comes first, and a product cannot shadow it. */
    for (int i = 0; i < source.count; ++i)
        jassert (! iconNames().contains (juce::String (source.originalFilenames[i]).upToLastOccurrenceOf (".svg", false, true)));
    sources.push_back (source);
}

bool isFilledIcon (const juce::String& name)
{
    return hasIcon (name) && filled (name);
}

std::unique_ptr<juce::Drawable> icon (const juce::String& name, juce::Colour colour)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto* source = IconStore::getInstance()->find (name);
    jassert (source != nullptr);   // not a glyph: neither the system's nor a product's
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
    jassert (glyph != nullptr);   // not a glyph: neither the system's nor a product's
    if (glyph == nullptr)
        return;

    /* The glyph's own 16px grid, centred: never scaled to the area, because
     * an icon is 16px inside its 28px control and never larger. */
    const auto origin = area.getCentre() - juce::Point<float> (8.0f, 8.0f);
    glyph->draw (g, 1.0f, juce::AffineTransform::translation (origin));
}

} // namespace uv

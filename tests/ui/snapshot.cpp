// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The snapshot harness. snapshot.h says what it promises and why.
 */
#include "snapshot.h"

#include "UvTokens.h"

#include <cmath>
#include <cstdlib>

namespace ni::ui::test
{

namespace
{
juce::File& baselinesOverride()
{
    static juce::File f;
    return f;
}

juce::File& outputOverride()
{
    static juce::File f;
    return f;
}

/* NI_UPDATE_BASELINES set to anything but "" or "0". */
bool updating()
{
    const char* v = std::getenv ("NI_UPDATE_BASELINES");
    return v != nullptr && *v != '\0' && juce::String (v) != "0";
}

juce::Result writePng (const juce::Image& image, const juce::File& file)
{
    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("cannot create " + file.getParentDirectory().getFullPathName());

    /* Replace, not append: a stale longer file would otherwise keep its
     * tail. */
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return juce::Result::fail ("cannot write " + file.getFullPathName());

    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, out))
        return juce::Result::fail ("cannot encode " + file.getFullPathName());

    return juce::Result::ok();
}

/* The pixels as one format, whatever the file or the renderer made them, so
 * two images compare channel by channel. Software image, ARGB. */
juce::Image asArgb (const juce::Image& image)
{
    return image.convertedToFormat (juce::Image::ARGB);
}
} // namespace

juce::String platform()
{
   #if JUCE_MAC
    return "darwin";
   #elif JUCE_LINUX || JUCE_BSD
    return "linux";
   #elif JUCE_WINDOWS
    return "windows";
   #else
    #error "no snapshot baselines are kept for this platform"
   #endif
}

juce::File baselineDir()
{
    return baselinesOverride() != juce::File() ? baselinesOverride() : juce::File (NI_UI_BASELINES);
}

juce::File outputDir()
{
    return outputOverride() != juce::File() ? outputOverride() : juce::File (NI_UI_SNAPSHOT_OUT);
}

void setDirsForTesting (const juce::File& baselines, const juce::File& output)
{
    baselinesOverride() = baselines;
    outputOverride() = output;
}

juce::Image render (juce::Component& component, float scale, bool opaque)
{
    jassert (! component.getLocalBounds().isEmpty());   // size it first
    jassert (scale > 0.0f);

    const auto bounds = component.getLocalBounds();

    /* JUCE's own renderer, by asking for a software image: the platform's
     * would make the picture depend on the machine (snapshot.h). */
    auto picture = component.createComponentSnapshot (bounds, true, scale, juce::SoftwareImageType());

    if (! opaque)
        return asArgb (picture);

    juce::Image out (juce::Image::ARGB, picture.getWidth(), picture.getHeight(), true,
                     juce::SoftwareImageType());
    {
        juce::Graphics g (out);
        g.fillAll (uv::tok::colour::bg000);
        g.drawImageAt (picture, 0, 0);
    }
    return out;
}

Diff diff (const juce::Image& actualIn, const juce::Image& baselineIn, int tolerance)
{
    const auto actual = asArgb (actualIn);
    const auto baseline = asArgb (baselineIn);

    Diff d;
    d.total = actual.getWidth() * actual.getHeight();

    if (actual.getBounds() != baseline.getBounds())
    {
        d.differing = juce::jmax (d.total, baseline.getWidth() * baseline.getHeight());
        d.picture = actual.createCopy();
        return d;
    }

    d.picture = juce::Image (juce::Image::ARGB, actual.getWidth(), actual.getHeight(), true,
                             juce::SoftwareImageType());

    const juce::Image::BitmapData a (actual, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData b (baseline, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData out (d.picture, juce::Image::BitmapData::writeOnly);

    const auto red = uv::tok::colour::red;
    const auto dark = uv::tok::colour::bg000;

    for (int y = 0; y < actual.getHeight(); ++y)
    {
        for (int x = 0; x < actual.getWidth(); ++x)
        {
            /* Unpremultiplied, as the PNG holds them. */
            const auto pa = a.getPixelColour (x, y);
            const auto pb = b.getPixelColour (x, y);

            const int delta = juce::jmax (std::abs ((int) pa.getAlpha() - (int) pb.getAlpha()),
                                          std::abs ((int) pa.getRed()   - (int) pb.getRed()),
                                          std::abs ((int) pa.getGreen() - (int) pb.getGreen()),
                                          std::abs ((int) pa.getBlue()  - (int) pb.getBlue()));

            /* Two fully transparent pixels are the same pixel whatever
             * colour their nothing is. */
            const bool same = delta <= tolerance
                           || (pa.getAlpha() == 0 && pb.getAlpha() == 0);

            if (! same)
                ++d.differing;

            out.setPixelColour (x, y, same ? dark.interpolatedWith (pb, 0.25f) : red);
        }
    }

    return d;
}

SnapshotResult compare (const juce::Image& actual, const juce::String& name,
                        const SnapshotOptions& options)
{
    jassert (name.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789-"));

    const auto file = name + "-" + platform();
    const auto baselineFile = baselineDir().getChildFile (file + ".png");
    const auto actualFile = outputDir().getChildFile (file + ".actual.png");
    const auto diffFile = outputDir().getChildFile (file + ".diff.png");

    SnapshotResult r;
    r.total = actual.getWidth() * actual.getHeight();

    if (updating())
    {
        const auto written = writePng (actual, baselineFile);
        r.ok = written.wasOk();
        r.message = written.wasOk() ? "baseline written: " + baselineFile.getFullPathName()
                                    : written.getErrorMessage();
        return r;
    }

    if (! baselineFile.existsAsFile())
    {
        writePng (actual, actualFile);
        r.message = "no baseline for " + platform() + ": " + baselineFile.getFullPathName()
                  + "\n  the render is at " + actualFile.getFullPathName()
                  + "\n  look at it, then write it with NI_UPDATE_BASELINES=1";
        return r;
    }

    const auto baseline = juce::ImageFileFormat::loadFrom (baselineFile);
    if (! baseline.isValid())
    {
        r.message = "cannot read the baseline " + baselineFile.getFullPathName();
        return r;
    }

    const auto d = diff (actual, baseline, options.tolerance);
    r.differing = d.differing;

    const auto allowed = (int) std::floor (options.maxDiffRatio * (double) r.total);
    const bool sizeMatches = actual.getBounds() == baseline.getBounds();

    if (sizeMatches && d.differing <= allowed)
    {
        r.ok = true;
        return r;
    }

    writePng (actual, actualFile);
    writePng (d.picture, diffFile);

    r.message = (sizeMatches
                     ? juce::String (d.differing) + " of " + juce::String (r.total)
                         + " pixels differ by more than " + juce::String (options.tolerance)
                         + " (allowed: " + juce::String (allowed) + ")"
                     : "the render is " + juce::String (actual.getWidth()) + "x"
                         + juce::String (actual.getHeight()) + ", the baseline "
                         + juce::String (baseline.getWidth()) + "x"
                         + juce::String (baseline.getHeight()))
              + "\n  baseline " + baselineFile.getFullPathName()
              + "\n  actual   " + actualFile.getFullPathName()
              + "\n  diff     " + diffFile.getFullPathName();
    return r;
}

SnapshotResult snapshot (juce::Component& component, const juce::String& name,
                         const SnapshotOptions& options)
{
    return compare (render (component, options.scale, options.opaque), name, options);
}

} // namespace ni::ui::test

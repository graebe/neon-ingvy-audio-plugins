// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The snapshot harness held to its own promises (snapshot.h): a missing
 * baseline fails, a picture within the numbers passes, one past them fails
 * with its render and its diff written, and NI_UPDATE_BASELINES writes
 * instead of comparing. In a directory of its own, never the real baselines.
 */
#include "snapshot.h"

#include "UvTokens.h"

#include <cstdlib>

using namespace ni::ui::test;

namespace
{
namespace c = uv::tok::colour;

/* A scratch pair of directories, the real ones restored afterwards. */
struct Scratch
{
    Scratch()
    {
        root = juce::File::createTempFile ("ni-ui-snapshots");
        root.createDirectory();
        setDirsForTesting (root.getChildFile ("baselines"), root.getChildFile ("out"));
    }
    ~Scratch()
    {
        setDirsForTesting ({}, {});
        root.deleteRecursively();
    }
    juce::File root;
};

/* NI_UPDATE_BASELINES for one scope. */
struct Updating
{
   #if JUCE_WINDOWS
    Updating()  { _putenv_s ("NI_UPDATE_BASELINES", "1"); }
    ~Updating() { _putenv_s ("NI_UPDATE_BASELINES", ""); }
   #else
    Updating()  { ::setenv ("NI_UPDATE_BASELINES", "1", 1); }
    ~Updating() { ::unsetenv ("NI_UPDATE_BASELINES"); }
   #endif
};

struct Square final : public juce::Component
{
    explicit Square (juce::Colour fill) : colour (fill) { setSize (40, 30); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (colour);
        g.fillRect (10, 10, 20, 10);
    }
    juce::Colour colour;
};

juce::Image flat (int w, int h, juce::Colour colour)
{
    juce::Image img (juce::Image::ARGB, w, h, false, juce::SoftwareImageType());
    juce::Graphics g (img);
    g.fillAll (colour);
    return img;
}
} // namespace

TEST_CASE ("snapshot harness: the platform names its baselines")
{
    const auto os = platform();
    CHECK ((os == "darwin" || os == "linux" || os == "windows"));
}

TEST_CASE ("snapshot harness: a render is the component at its size and scale, on bg-000")
{
    Square square (c::uv);
    const auto one = render (square);
    CHECK (one.getWidth() == 40);
    CHECK (one.getPixelAt (0, 0) == c::bg000);    // opaque by default
    CHECK (one.getPixelAt (15, 15) == c::uv);

    const auto two = render (square, 2.0f);
    CHECK (two.getWidth() == 80);
    CHECK (two.getPixelAt (30, 30) == c::uv);

    CHECK (render (square, 1.0f, false).getPixelAt (0, 0).getAlpha() == 0);
}

TEST_CASE ("snapshot harness: a pixel differs past the tolerance, and only then")
{
    const auto a = flat (10, 10, c::bg000);
    auto b = a.createCopy();
    b.setPixelAt (3, 3, c::bg000.interpolatedWith (c::bg100, 0.25f));   // two levels
    b.setPixelAt (5, 5, c::bg100);                                      // eight: bg-000 to bg-100

    const auto loose = diff (a, b, 10);
    CHECK (loose.differing == 0);

    const auto strict = diff (a, b, 4);
    CHECK (strict.differing == 1);   // the eight, not the two
    CHECK (strict.picture.getPixelAt (5, 5) == c::red);

    const auto sized = diff (a, flat (11, 10, c::bg000), 4);
    CHECK (sized.differing >= 100);
}

TEST_CASE ("snapshot harness: a missing baseline fails, and leaves the render to look at")
{
    Scratch scratch;
    Square square (c::uv);
    const auto r = snapshot (square, "square");
    CHECK_FALSE (r.ok);
    CHECK (r.message.contains ("no baseline"));
    CHECK (outputDir().getChildFile ("square-" + platform() + ".actual.png").existsAsFile());
}

TEST_CASE ("snapshot harness: NI_UPDATE_BASELINES writes, then the same picture passes")
{
    Scratch scratch;
    Square square (c::uv);
    {
        Updating updating;
        const auto written = snapshot (square, "square");
        CHECK (written.ok);
        CHECK (written.message.contains ("baseline written"));
    }
    CHECK (baselineDir().getChildFile ("square-" + platform() + ".png").existsAsFile());
    CHECK (snapshot (square, "square").ok);
}

TEST_CASE ("snapshot harness: a changed picture fails, with its render and its diff")
{
    Scratch scratch;
    {
        Updating updating;
        Square before (c::uv);
        REQUIRE (snapshot (before, "square").ok);
    }

    Square after (c::amber);   // the same square in the wrong colour: 200 pixels
    const auto r = snapshot (after, "square");
    CHECK_FALSE (r.ok);
    CHECK (r.differing == 200);
    CHECK (r.message.contains ("200 of 1200"));
    CHECK (outputDir().getChildFile ("square-" + platform() + ".actual.png").existsAsFile());
    CHECK (outputDir().getChildFile ("square-" + platform() + ".diff.png").existsAsFile());

    /* With numbers that allow it -- said at the call -- the same picture
     * passes. */
    SnapshotOptions lenient;
    lenient.maxDiffRatio = 0.2;
    CHECK (snapshot (after, "square", lenient).ok);
}

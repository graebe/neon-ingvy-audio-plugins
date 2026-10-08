// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Snapshot goldens: a Component rendered to pixels and held to a committed
 * picture of itself.
 *
 *   NI_SNAPSHOT_TEST ("knob: every state")
 *   {
 *       KnobPage page;                      // sized by itself
 *       NI_CHECK_SNAPSHOT (page, "knob-states");
 *   }
 *
 * THE BASELINE is tests/ui/baselines/<name>-<os>.png, <os> being darwin, linux
 * or windows: one set per platform, because the platforms rasterise a glyph outline differently and a baseline
 * from one is not the truth on another. A test with no baseline for its
 * platform FAILS, saying so; it never passes by having nothing to compare.
 *
 *   NI_UPDATE_BASELINES=1 ctest --test-dir build-ui -R snapshots
 *
 * writes the baselines of every snapshot that runs instead of comparing, and
 * the test passes. Narrow it with doctest's own filter so only the goldens
 * meant to change are rewritten:
 *
 *   NI_UPDATE_BASELINES=1 build-ui/.../ni_ui_tests --test-suite=snapshot --test-case='*knob*'
 *
 * and look at every rewritten PNG before committing it: a baseline is a claim
 * that the picture is right.
 *
 * ON A FAILED COMPARISON the render and a diff are written beside each other in
 * the build tree, NI_UI_SNAPSHOT_OUT (build-ui/tests/ui/snapshots/):
 *
 *   <name>-<os>.actual.png   what the component drew
 *   <name>-<os>.diff.png     the baseline, dimmed to a quarter, with every
 *                            differing pixel in red (uv::tok::colour::red)
 *
 * and the failure message names both files and how many pixels differed.
 *
 * THE NUMBERS. A pixel differs when any channel, A, R, G or B, is more than
 * `tolerance` = 4 (of 255) away from the baseline's. A comparison fails when
 * more than `maxDiffRatio` = 0.01 % of the pixels differ (61 of the gallery's
 * 960 x 640; none of a 48 x 48 knob's). Why those:
 *
 *   - 4 is under half the smallest step between two of the system's grounds
 *     (bg-000 and bg-100 are 8 apart), so a control painted in the wrong
 *     ground fails, while a renderer that rounds a coverage value one way on
 *     arm64 and the other on x86_64 does not.
 *   - 0.01 % lets a handful of edge pixels move, and nothing more: a hairline
 *     moved by 1px along 100px is 200 pixels; a recoloured 28px button is
 *     hundreds. What it will not see is one changed glyph in a whole editor
 *     -- an editor's strings are its unit tests' to pin, not its picture's.
 *
 * Both are per call (SnapshotOptions), for the rare picture that needs other
 * numbers -- with the reason beside the call.
 *
 * DETERMINISTIC BY CONSTRUCTION. The render is JUCE's own software renderer
 * (juce::SoftwareImageType), not the platform's: the same code on every OS, no
 * GPU, no display, no window, so a test runs headless and twice gives the same
 * pixels. Every glyph comes from the embedded faces -- the test main makes
 * uv::LookAndFeel the default, which maps any font JUCE makes for itself onto
 * them -- so no installed font can change a picture. What still differs
 * between platforms is how a glyph's outline is read from the font, which is
 * what the per-OS baselines are for.
 */
#pragma once

#include <doctest.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui::test
{

struct SnapshotOptions
{
    /* Device pixels per design pixel: 1, or 2 for a Retina-sized picture. */
    float scale = 1.0f;
    /* The largest channel difference (0..255) that is still the same pixel. */
    int tolerance = 4;
    /* The share of pixels allowed to differ: 0.0001 is 0.01 %. */
    double maxDiffRatio = 0.0001;
    /* Composite the render onto bg-000 first, so a component that leaves
     * parts of itself transparent is pictured as it looks in a window. */
    bool opaque = true;
};

struct SnapshotResult
{
    bool ok = false;
    juce::String message;
    int differing = 0;
    int total = 0;
};

/* darwin, linux or windows: the suffix of this platform's baselines. */
juce::String platform();

/* The component as pixels: its current size (it must have one) times
 * `scale`, drawn by JUCE's software renderer; on bg-000 when `opaque`. */
juce::Image render (juce::Component&, float scale = 1.0f, bool opaque = true);

/* How two images differ: the count of pixels whose largest channel
 * difference exceeds `tolerance`, and the picture of where. Images of
 * different sizes differ everywhere. */
struct Diff
{
    int differing = 0;
    int total = 0;
    juce::Image picture;
};
Diff diff (const juce::Image& actual, const juce::Image& baseline, int tolerance);

/* A render held to tests/ui/baselines/<name>-<os>.png, or written there
 * when NI_UPDATE_BASELINES is set. `name` is lower-case words and dashes. */
SnapshotResult compare (const juce::Image& actual, const juce::String& name,
                        const SnapshotOptions& = {});

/* render() then compare(). */
SnapshotResult snapshot (juce::Component&, const juce::String& name,
                         const SnapshotOptions& = {});

/* The directories, from the build: where baselines are read and written,
 * and where a failure's pictures go. Overridable for the harness's own
 * tests. */
juce::File baselineDir();
juce::File outputDir();
void setDirsForTesting (const juce::File& baselines, const juce::File& output);

} // namespace ni::ui::test

/* A golden: a test case in the suite the full tier runs. */
#define NI_SNAPSHOT_TEST(title) TEST_CASE (title * doctest::test_suite ("snapshot"))

/* Holds `component` to the baseline `name`, reporting the caller's line. */
#define NI_CHECK_SNAPSHOT(component, name)                                          \
    do {                                                                           \
        const auto niSnapshot_ = ::ni::ui::test::snapshot ((component), (name));   \
        CHECK_MESSAGE (niSnapshot_.ok, niSnapshot_.message.toStdString());         \
    } while (false)

/* The same, with numbers of its own: say why at the call. */
#define NI_CHECK_SNAPSHOT_WITH(component, name, options)                                     \
    do {                                                                                    \
        const auto niSnapshot_ = ::ni::ui::test::snapshot ((component), (name), (options)); \
        CHECK_MESSAGE (niSnapshot_.ok, niSnapshot_.message.toStdString());                  \
    } while (false)

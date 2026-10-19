// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A FREQUENCY RESPONSE WELL: the picture an equaliser is drawn on -- NI
 * Filter Designer's ResponseWell, as the kit's. 10 Hz to 22 kHz in equal
 * steps of pitch across, a dB range up (±6, ±12, ±18 or ±30, clicked in the
 * caption), and, behind it all, an analyzer.
 *
 * WHAT IT DRAWS (paintBackdrop), BACK TO FRONT:
 *
 *   the analyzer   the spectrum before the filter (pre) faded behind, as
 *                  plot-dry at a quarter, and after it (post) in front of
 *                  it, plot-ghost under an ink-dim edge -- on its own scale,
 *                  -6 to -84 dBFS; Pre, Post, both or off
 *   the rulers     frequency under, dB at the left; no grid lines
 *   0 dB           the grey line: the signal untouched
 *
 * and, in the caption's right end (paintCaptionEnd), the analyzer's mode and
 * the range, each a click (captionHitAt). What goes over the backdrop -- the
 * curves, the handles -- is the product's: it derives, paints its marks in
 * paintPlot() after paintBackdrop(), and builds them with curvePath().
 *
 * A PHASE SCALE, for a filter whose magnitude is flat (an all-pass): ±180°
 * named at the right edge (withPhaseScale), its curve from phasePath(),
 * broken where it wraps.
 *
 * DRAW WHAT THE ENGINE RENDERS (Plot.h): every curve and spectrum handed in
 * is the engine's, at the frequencies it gives with them.
 *
 * Message thread.
 */
#pragma once

#include "Plot.h"

#include <optional>

namespace ni::ui
{

class FrequencyResponse : public PlotWell
{
public:
    static constexpr double lowestHz = 10.0, highestHz = 22000.0;
    /* The rulers' room: dB labels at the left, Hz labels under, the phase
     * scale's at the right. */
    static constexpr float labelsLeft = 30.0f, labelsUnder = 14.0f, labelsRight = 34.0f;
    /* The analyzer's own scale, in dBFS: its top and its depth. */
    static constexpr double analyzerTop = -6.0, analyzerSpan = 78.0;

    /* The analyzer's modes, as an engine's Analyzer choice numbers them. */
    enum Analyzer : int
    {
        analyzerOff = 0,
        analyzerPre,
        analyzerPost,
        analyzerBoth
    };

    FrequencyResponse();
    ~FrequencyResponse() override;

    /* A ±180° phase scale at the right edge, and room for its labels. */
    void withPhaseScale (bool);
    bool hasPhaseScale() const noexcept { return phaseScale; }

    /* ---- the geometry, in the well's coordinates */
    juce::Rectangle<float> plotArea() const;
    float xOf (double hz) const;
    double hzAt (float x) const;
    float yOf (double db) const;
    double dbAt (float y) const;
    float yOfDegrees (double degrees) const;

    int rangeDb() const noexcept { return range; }
    /* ±6, ±12, ±18, ±30, round again. */
    void cycleRange();

    /* A line through (`hz`[i], `db`[i]), held a pixel past the plot's edges. */
    juce::Path curvePath (const float* hz, const float* db, int count) const;
    /* A phase curve, in degrees -180..180, broken where it wraps. */
    juce::Path phasePath (const float* hz, const float* degrees, int count) const;

    /* The analyzer: `live` while a window watches, its spectra in dBFS at
     * `hz`, and the mode it is drawn in (Analyzer). */
    void setAnalyzer (int mode, bool live, const float* hz, const float* pre, const float* post, int count);
    int analyzerMode() const noexcept { return mode; }

    /* What a press in the caption's right end is. */
    enum class CaptionHit
    {
        analyzer,
        range
    };
    std::optional<CaptionHit> captionHitAt (juce::Point<float>) const;

protected:
    /* The analyzer, the rulers and 0 dB: what a product paints first. */
    void paintBackdrop (juce::Graphics&);
    /* The analyzer's mode and the range, at the caption's right end. */
    void paintCaptionEnd (juce::Graphics&);

    /* What the derived well does when the geometry moved (resized, a new
     * range): rebuild its paths. */
    virtual void geometryChanged() {}

    void resized() override;

private:
    void rebuildAnalyzer();
    juce::String rangeText() const;

    int range = 18;
    int mode = analyzerBoth;
    bool live = false;
    bool phaseScale = false;
    std::vector<float> hzs, pres, posts;
    juce::Path pre, post, postEdge;

    JUCE_DECLARE_NON_COPYABLE (FrequencyResponse)
};

} // namespace ni::ui

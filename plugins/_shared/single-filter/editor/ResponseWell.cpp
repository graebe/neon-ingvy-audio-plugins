// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A single filter's response well. ResponseWell.h says what it shows and
 * what the pointer does.
 */
#include "ResponseWell.h"

#include "InfoLines.h"
#include "UvTokens.h"

#include <cmath>

namespace ni::sf
{

namespace
{
namespace c = uv::tok::colour;

/* How near the pointer must be to take an edge tick. */
constexpr float reach = 6.0f;
/* A drag of this many pixels doubles or halves Q. */
constexpr double pxPerQOctave = 48.0;
} // namespace

ResponseWell::ResponseWell (Model& m)
    : model (m),
      qIndex (m.indexOf (field::q)),
      gainIndex (m.indexOf (field::gain)),
      freq (m.parameter (m.indexOf (field::freq)))
{
    ni::ui::setInfo (*this, m.shape() == shape::allPass ? info::phase : info::response);
    withPhaseScale (m.shape() == shape::allPass);

    const auto alongFreq = [this] (float hz, float px)
    {
        const double pxPerOctave = plotArea().getWidth() / std::log2 (highestHz / lowestHz);
        return (float) juce::jlimit (lowestHz, highestHz, hz * std::pow (2.0, px / pxPerOctave));
    };
    const auto alongGain = [this] (float db, float px) { return (float) juce::jlimit (-30.0, 30.0, db - px * 2.0 * rangeDb() / plotArea().getHeight()); };
    const auto alongQ = [] (float q, float px) { return (float) juce::jlimit (0.1, 40.0, q * std::pow (2.0, -px / pxPerQOctave)); };

    /* Up and down: Gain where the filter has one, Q where it has that; Alt
     * on a gain handle is its Q. */
    if (gainIndex >= 0)
    {
        vertical = std::make_unique<ni::ui::ParamBinding> (m.parameter (gainIndex));
        if (qIndex >= 0)
            alt = std::make_unique<ni::ui::ParamBinding> (m.parameter (qIndex));
    }
    else if (qIndex >= 0)
        vertical = std::make_unique<ni::ui::ParamBinding> (m.parameter (qIndex));

    dot = std::make_unique<ni::ui::PlotHandle> (*this, freq, alongFreq, vertical.get(),
                                                vertical == nullptr ? ni::ui::PlotHandle::Travel {} : gainIndex >= 0 ? ni::ui::PlotHandle::Travel (alongGain) : ni::ui::PlotHandle::Travel (alongQ));
    if (alt != nullptr)
        dot->setAltY (alt.get(), alongQ);
    dot->setTitle ("Freq and " + juce::String (gainIndex >= 0 ? "Gain" : qIndex >= 0 ? "Q" : ""));
    ni::ui::setInfo (*dot, info::handle);
    addAndMakeVisible (*dot);
    setCaption ("RESPONSE");
    update();
}

ResponseWell::~ResponseWell() = default;

double ResponseWell::plain (int f) const
{
    const int index = model.indexOf (f);
    if (index < 0)
        return 0.0;
    auto& p = model.parameter (index);
    return (double) p.convertFrom0to1 (p.getValue());
}

juce::String ResponseWell::captionText() const
{
    static const char* const lanes[] = { "STEREO (MID)", "LEFT", "RIGHT", "MID", "SIDE" };
    juce::String text (model.shape() == shape::allPass ? "PHASE" : "RESPONSE");
    text << "   " << lanes[juce::jlimit (0, 4, (int) plain (field::channel))];
    if (shown.sampleRate > 0.0)
        text << "   " << juce::String (shown.sampleRate / 1000.0, juce::exactlyEqual (shown.sampleRate, std::floor (shown.sampleRate / 1000.0) * 1000.0) ? 0 : 1) << " KHZ";
    return text;
}

void ResponseWell::update()
{
    const auto& p = model.picture();
    std::array<double, 16> now {};
    for (int i = 0; i < model.numParameters() && i < (int) now.size(); ++i)
    {
        auto& q = model.parameter (i);
        now[(size_t) i] = q.convertFrom0to1 (q.getValue());
    }
    const bool same = hasPicture && now == values && p.serial == shown.serial && p.total == shown.total && p.phase == shown.phase
                      && p.analyzer == shown.analyzer && p.pre == shown.pre && p.post == shown.post;
    if (same)
        return;
    shown = p;
    values = now;
    hasPicture = true;
    setCaption (captionText());
    setAnalyzer ((int) plain (field::analyzer), shown.analyzer, shown.hz.data(), shown.pre.data(), shown.post.data(), Picture::points);
    rebuild();
    repaint();
}

juce::Point<float> ResponseWell::handleAt() const
{
    /* On the curve drawn: where the engine puts it on the filter alone,
     * lifted by Output as the curve is. */
    const auto a = plotArea();
    const double db = (std::isnan (shown.handleDb) ? 0.0 : shown.handleDb) + plain (field::output);
    const double hz = std::isnan (shown.handleHz) ? plain (field::freq) : shown.handleHz;
    return { xOf (hz), juce::jlimit (a.getY(), a.getBottom(), yOf (db)) };
}

void ResponseWell::geometryChanged()
{
    rebuild();
}

void ResponseWell::rebuild()
{
    widthTicks.clear();
    line.clear();
    if (! hasPicture)
        return;
    line = hasPhaseScale() ? phasePath (shown.hz.data(), shown.phase.data(), Picture::points)
                           : curvePath (shown.hz.data(), shown.total.data(), Picture::points);
    const auto h = handleAt();
    for (const double edge : shown.edgeHz)
        if (! std::isnan (edge))
        {
            widthTicks.startNewSubPath (xOf (edge), h.y - 6.0f);
            widthTicks.lineTo (xOf (edge), h.y + 6.0f);
        }
    dot->setBounds (juce::Rectangle<int> (ni::ui::PlotHandle::size, ni::ui::PlotHandle::size).withCentre (h.roundToInt()));
}

void ResponseWell::paintPlot (juce::Graphics& g)
{
    paintBackdrop (g);
    const auto a = plotArea();
    g.saveState();
    g.reduceClipRegion (a.toNearestInt().expanded (0, 2));
    ni::ui::plot::curve (g, line);
    g.setColour (c::inkDim);
    g.strokePath (widthTicks, juce::PathStrokeType (1.0f));
    /* THE ONE AMBER MARK: where the rate clamps the filter's band. */
    if (shown.clamped && shown.sampleRate > 0.0)
        ni::ui::plot::rule (g, xOf (0.49 * shown.sampleRate), a.getY(), a.getBottom(), true);
    g.restoreState();
    paintCaptionEnd (g);
}

bool ResponseWell::nearEdge (juce::Point<float> p) const
{
    const auto h = handleAt();
    for (const double edge : shown.edgeHz)
        if (! std::isnan (edge) && std::abs (xOf (edge) - p.x) < reach && std::abs (h.y - p.y) < 2.0f * reach)
            return true;
    return false;
}

void ResponseWell::mouseDown (const juce::MouseEvent& e)
{
    if (const auto hit = captionHitAt (e.position))
    {
        if (*hit == CaptionHit::range)
            cycleRange();
        else if (const int index = model.indexOf (field::analyzer); index >= 0)
        {
            /* Pre + post, Post, Pre, off, round again. */
            auto& p = model.parameter (index);
            const int next = ((int) plain (field::analyzer) + 3) % 4;
            p.beginChangeGesture();
            p.setValueNotifyingHost (p.convertTo0to1 ((float) next));
            p.endChangeGesture();
            update();
        }
        return;
    }
    if (qIndex >= 0 && nearEdge (e.position))
    {
        draggingEdge = true;
        model.parameter (qIndex).beginChangeGesture();
    }
}

void ResponseWell::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingEdge)
        return;
    auto& p = model.parameter (qIndex);
    p.setValueNotifyingHost (p.convertTo0to1 ((float) model.qForEdge (plain (field::freq), hzAt (e.position.x))));
    update();
}

void ResponseWell::mouseUp (const juce::MouseEvent&)
{
    if (draggingEdge)
        model.parameter (qIndex).endChangeGesture();
    draggingEdge = false;
}

void ResponseWell::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (qIndex < 0 || wheel.deltaY == 0.0f)
        return;
    auto& p = model.parameter (qIndex);
    const double q = plain (field::q);
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 ((float) juce::jlimit (0.1, 40.0, q * std::pow (2.0, (double) wheel.deltaY * 2.0))));
    p.endChangeGesture();
}

} // namespace ni::sf

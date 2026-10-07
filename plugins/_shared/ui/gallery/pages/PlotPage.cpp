// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The PlotWell as its card shows it -- an envelope with the gate's ghost
 * behind it on a millisecond ruler, and a signal, dry in grey with the gated
 * trace in front -- and a third well for the states the card names but does
 * not picture: a capture still filling, and the amber rule of a curve that
 * runs past its step.
 *
 * The data is made up here, deterministically, so the picture holds still; an
 * editor's plot draws what its engine rendered.
 */
#include "DisplayPage.h"
#include "Gallery.h"

#include "Info.h"
#include "Plot.h"

#include <array>
#include <cmath>

namespace
{

using namespace ni::ui;

constexpr int wellW = 680, wellH = 104;
/* Ruler line and the floor of the picture over it, as the Trance Gate's
 * envelope lays them out. */
constexpr float axisY = (float) wellH - plot::inset - plot::labelDrop - 2.0f;
constexpr float floorY = axisY - 2.0f;

struct EnvelopeWell final : public PlotWell
{
    EnvelopeWell()
    {
        setCaption ("Envelope   one step, 125 ms   as dialled behind");
        setInfo (*this, juce::String::fromUTF8 ("Envelope plot \xe2\x80\x94 one gate as the engine plays it, on a ms axis."));
    }

    void resized() override { axis.set ((float) getWidth(), 160.0, 125.0); }

    void paintPlot (juce::Graphics& g) override
    {
        const auto content = contentBounds();
        const float top = content.getY();
        const auto yOf = [&] (float level) { return floorY - (floorY - top) * level; };

        /* Attack to full, decay to 0.6, hold, then the gate closes at 0.58
         * and the release falls to nothing by 0.74. */
        const auto level = [] (float t)
        {
            if (t < 0.05f) return t / 0.05f;
            if (t < 0.24f) return 1.0f - 0.4f * (t - 0.05f) / 0.19f;
            if (t < 0.58f) return 0.6f;
            if (t < 0.74f) return 0.6f * (1.0f - (t - 0.58f) / 0.16f);
            return 0.0f;
        };

        juce::Path line, area, behind;
        bool started = false;
        const int n = 256;
        for (int i = 0; i <= n; ++i)
        {
            const float t = (float) i / (float) n;
            const juce::Point<float> p { xAt (t), yOf (level (t)) };
            i == 0 ? line.startNewSubPath (p) : line.lineTo (p);
            if (t >= 0.58f)
            {
                /* The level as dialled, where the gate cut it short. */
                if (! started)
                    behind.startNewSubPath (p.x, yOf (0.6f));
                else
                    behind.lineTo (p.x, yOf (0.6f));
                started = true;
            }
        }
        area = line;
        area.lineTo (xAt (1.0f), floorY);
        area.lineTo (xAt (0.0f), floorY);
        area.closeSubPath();

        plot::rule (g, xAt (0.58f), top, floorY);
        plot::under (g, area);
        plot::ghost (g, behind);
        plot::curve (g, line);
        axis.paint (g, axisY);
    }

    plot::Axis axis;
};

/* A capture as the engines publish one: dry lo/hi, wet lo/hi per column. */
struct Scope
{
    static constexpr int columns = 256, stride = 4;
    std::array<float, columns * stride> data {};

    Scope()
    {
        for (int i = 0; i < columns; ++i)
        {
            const float t = (float) i / (float) columns;
            /* A swelling tone with a little texture: deterministic, not noise. */
            const float body = 0.45f + 0.25f * std::sin (t * 6.2832f * 7.0f) + 0.05f * std::sin ((float) i * 2.3f);
            const bool open = std::fmod (t * 8.0f, 1.0f) < 0.45f;
            auto* col = data.data() + i * stride;
            col[0] = -body;
            col[1] = body;
            col[2] = open ? -body : 0.0f;
            col[3] = open ? body : 0.0f;
        }
    }
};

struct SignalWell final : public PlotWell
{
    explicit SignalWell (bool isFilling) : filling (isFilling)
    {
        setCaption (filling ? "Signal   filling   the first cycle since play"
                            : "Signal   one cycle, 500 ms   dry in grey, gated in front");
        setInfo (*this, juce::String::fromUTF8 ("Signal plot \xe2\x80\x94 the dry input in grey, the gated output in violet."));
    }

    void paintPlot (juce::Graphics& g) override
    {
        const auto content = contentBounds();
        const float bottom = content.getBottom();
        const float mid = (content.getY() + bottom) * 0.5f;
        g.setColour (uv::tok::colour::line100);
        g.drawLine (content.getX(), mid, content.getRight(), mid, 1.0f);

        const plot::Capture cap { scope.data.data(), Scope::stride, Scope::columns };
        const plot::BandGeometry geom { content.getX(), (int) content.getWidth(), content.getY(), bottom };
        const auto seen = [this] (int column) { return ! filling || column < Scope::columns * 3 / 5; };

        plot::band (path, cap, 0, 1, geom, seen);
        plot::dry (g, path);
        plot::band (path, cap, 2, 3, geom, seen);
        plot::wet (g, path);

        /* Where the step ends, with the release running past it: the window's
         * one amber mark. */
        if (filling)
            plot::rule (g, xAt (0.45f), content.getY(), bottom, true);
    }

    bool filling;
    Scope scope;
    juce::Path path;
};

struct PlotPage final : public gallery::DisplayPage
{
    static constexpr int row = 14 + 8 + wellH + 16;

    PlotPage() : signal (false), filling (true)
    {
        setSize (wellW, 3 * row - 16);
        place (envelope, "envelope", 0);
        place (signal, "signal", 1);
        place (filling, "filling, past the step", 2);
    }

    void place (PlotWell& well, const char* name, int i)
    {
        const int y = i * row;
        caption (name, { 0.0f, (float) y, (float) wellW, 14.0f });
        addAndMakeVisible (well);
        well.setBounds (0, y + 22, wellW, wellH);
    }

    EnvelopeWell envelope;
    SignalWell signal, filling;
};

} // namespace

NI_GALLERY_PAGE ("Display", "Plot", [] { return std::make_unique<PlotPage>(); });

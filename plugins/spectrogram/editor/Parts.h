// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The pieces of NI Spectrogram's window that are not kit controls: the words
 * between the controls, the well the picture sits in, the frequency scale
 * beside it, the time axis under it and the crosshair's readout. The web
 * editor's Display.jsx, SourceStrip.jsx and Toolbar.jsx, less their controls,
 * and app.css's geometry for them.
 *
 * THE SCALES ARE OUTSIDE THE WELL, not laid over the picture: over it a label
 * is legible against silence and invisible against a loud partial, and fixing
 * that needs a floating plate, which nothing in this system is.
 *
 * None of these takes the pointer or the keyboard except the well's picture;
 * the words are words, and the hint names the controls beside them.
 */
#pragma once

#include "Scales.h"
#include "Spectrogram.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace ni::spectrogram
{

/*
 * One word in a text style: a group's caption (label, ink-muted), the "vs"
 * between the two comparison pickers (hint, ink-muted), and the window's one
 * amber mark, "no signal" (label, amber). Its ideal width is its text's.
 */
class Words final : public juce::Component
{
public:
    enum class Voice { caption, quiet, warning };

    explicit Words (Voice, const juce::String& text = {});

    void setText (const juce::String&);
    const juce::String& getText() const noexcept { return text; }
    int idealWidth() const;

    void paint (juce::Graphics&) override;

private:
    const Voice voice;
    juce::String text;
};

/* The hint bar's rule turned on its side, inset space-1 top and bottom, so it
 * reads as a separator between two groups rather than a border round either. */
class Divider final : public juce::Component
{
public:
    Divider();
    void paint (juce::Graphics&) override;
};

/*
 * The picture's well: bg-000 on a line-200 hairline, the plots' frame. A plot
 * is not a control well -- its ground has to be the picture's own zero, or
 * silence would draw as a colour. TWO PIXELS BIGGER THAN THE PICTURE both
 * ways, so the frame eats none of it: the web editor once lost its bottom two
 * bands to a frame exactly as tall as the canvas. The well, not the picture,
 * is the wall the Ground's rings break around.
 */
class Well final : public juce::Component
{
public:
    Well();

    ni::ui::Spectrogram& picture() noexcept { return spectrogram; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ni::ui::Spectrogram spectrogram;
};

/*
 * The frequency scale: each mark's label, right-aligned, and a 4 px tick
 * pointing at the height it names, beside the picture -- placed one pixel
 * down, where the picture starts inside the well's frame.
 *
 * A LABEL AT AN END HANGS PAST IT: 20k is centred on the picture's top edge
 * and 10 on its bottom, as the web scale's overflow let them. So the
 * component reaches `overhang` above and below the well it stands beside.
 */
class FrequencyScale final : public juce::Component
{
public:
    static constexpr int overhang = 8;

    FrequencyScale();

    void setMarks (std::vector<FreqMark>);
    const std::vector<FreqMark>& getMarks() const noexcept { return marks; }

    void paint (juce::Graphics&) override;

private:
    std::vector<FreqMark> marks;
};

/*
 * The time axis, on the picture's x: a 1 x 4 px tick per mark, its label
 * under it, the end labels kept inside the picture. In the bar view the bar
 * tick takes its label's colour, ink-dim, and the beat tick the rail: a
 * beat's tick has no number and IS the mark, and a line-200 hairline alone
 * loses against the Ground's dots right behind it.
 */
class TimeAxis final : public juce::Component
{
public:
    /* The first bar's number is centred on the picture's left edge and hangs
     * past it; the component reaches this far either side of the picture. */
    static constexpr int overhang = 16;

    TimeAxis();

    void setMarks (std::vector<TimeMark>, bool barView);
    const std::vector<TimeMark>& getMarks() const noexcept { return marks; }
    bool isBarView() const noexcept { return bars; }

    void paint (juce::Graphics&) override;

private:
    std::vector<TimeMark> marks;
    bool bars = false;
};

/*
 * The crosshair's readout: three keys in the label voice and their values in
 * value type, on one baseline, each value at least 76 px wide ("< −96 dB"),
 * so the row does not move as the pointer does. Not a control and never
 * framed: a line of facts about what is on screen.
 */
class CrosshairReadout final : public juce::Component
{
public:
    CrosshairReadout();

    /* The middle key: "time", or "pos" in the bar view. */
    void setValues (const juce::String& freq, const juce::String& timeKey, const juce::String& time,
                    const juce::String& level);

    const juce::String& getFrequency() const noexcept { return freq; }
    const juce::String& getTimeKey() const noexcept { return timeKey; }
    const juce::String& getTime() const noexcept { return time; }
    const juce::String& getLevel() const noexcept { return level; }

    void paint (juce::Graphics&) override;

private:
    juce::String freq, timeKey { "time" }, time, level;
};

} // namespace ni::spectrogram

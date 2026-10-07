// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Spectrogram's editor: the web editor's App.jsx and its three parts
 * (Toolbar, SourceStrip, Display), natively, on Ultraviolet 1.1.0.
 *
 * THE WINDOW, 720 x 502, laid out at that size always and scaled to fit
 * (FixedDesign). Inside the EditorFrame's padding, a 656 x 402 content, top
 * to bottom on the system's 4 px grid:
 *
 *    28   the toolbar: the one amber word on the left when nothing arrives;
 *         Range, the bars switch, how many bars, Pause on the right
 *    16   space-4
 *    28   the source strip: VIEW and its list, a rule, COMPARE a vs b and
 *         the clash switch
 *    16   space-4
 *   258   the frequency scale (40) and space-2 beside the well (608): a
 *         606 x 256 picture in a 1 px frame, one column and one band a pixel
 *     8   space-2
 *    16   the time axis, on the picture's x (49 in)
 *    12   space-3
 *    20   the crosshair's readout: freq, time or pos, level
 *
 * and the frame's hint bar under it: the window's conventions, Motion and
 * the Signature. Per Ultraviolet's Hint card the bar states conventions
 * only; the facts the web editor printed there (the span, the floor, the
 * history) are where the 1.1.0 layouts put them -- in the Range select, the
 * level readout and the time axis (proposal S2).
 *
 * WHAT IS WHOSE. The session -- the view, the comparison, the clash, the
 * zoom -- is the model's, saved with the set: a control shows what
 * model.session() holds, and asks the model to change it. Pause, the bar
 * view and its width are this window's and are not saved (the manual: "a
 * reopened window always starts live, in seconds"). The picture's history is
 * the Spectrogram component's ring, written from the columns the model hands
 * over.
 *
 * ONE TICK, ON THE FRAME CLOCK (update): take the columns finished since the
 * last, place each at its bar-view slot, and pick up whatever the plugin
 * changed -- a session loaded by the host, a bus inserted, a new axis, the
 * metre. The tick reads and repaints; it decides nothing that a late tick
 * would make wrong: columns wait in the plugin until taken, and "no signal"
 * is the time since the last one, by the clock.
 *
 * Message thread.
 */
#pragma once

#include "Button.h"
#include "CheckList.h"
#include "EditorFrame.h"
#include "Fit.h"
#include "Luminous.h"
#include "Model.h"
#include "Parts.h"
#include "Select.h"
#include "Toggle.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ni::spectrogram
{

/* The window's content: everything inside the frame's padding. */
class SpectrogramView final : public juce::Component,
                              public ni::ui::Luminous
{
public:
    static constexpr int width = 656;
    static constexpr int height = 402;

    /* Half a second without a column is a stall: one arrives every ~21 ms. */
    static constexpr double stallMs = 500.0;

    /* `model` must outlive the view. */
    explicit SpectrogramView (Model& model);
    ~SpectrogramView() override;

    /* One frame at `nowMs`: the columns, then whatever the plugin changed. */
    void update (double nowMs);

    /* Whether columns are arriving; "no signal" while they are not. */
    bool isLive() const noexcept { return live; }
    bool isPaused() const noexcept { return paused; }
    bool isBarView() const noexcept { return barView; }
    /* The bar view's width, in bars. */
    int bars() const;

    /* ---- the parts, for the editor's tests */
    Words& status() noexcept { return noSignal; }
    ni::ui::Select& rangeSelect() noexcept { return range; }
    ni::ui::Toggle& barsSwitch() noexcept { return barsToggle; }
    ni::ui::Select& barCountSelect() noexcept { return barCount; }
    ni::ui::Button& pauseButton() noexcept { return pause; }
    ni::ui::CheckList& viewList() noexcept { return view; }
    ni::ui::Select& compareSelect() noexcept { return compareA; }
    ni::ui::Select& againstSelect() noexcept { return compareB; }
    ni::ui::Toggle& clashSwitch() noexcept { return clash; }
    Well& well() noexcept { return pictureWell; }
    ni::ui::Spectrogram& picture() noexcept { return pictureWell.picture(); }
    FrequencyScale& frequencyScale() noexcept { return scale; }
    TimeAxis& timeAxis() noexcept { return axis; }
    CrosshairReadout& readout() noexcept { return reading; }

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;

private:
    /* What the controls last showed of the model, to see what moved. */
    struct Shown
    {
        Session session;
        std::vector<Source> sources;
        std::vector<float> hz;
        Transport transport;
        bool any = false;
    };

    void takeColumns (double nowMs);
    void syncSession (bool force);
    void syncSources (bool force);
    void syncAxis (bool force);
    void syncTransport (bool force);
    void refreshTimeMarks();
    void refreshReadout (const std::optional<ni::ui::Spectrogram::Sample>&);

    /* The user's choices, as the web editor's handlers. */
    void chooseRange (int index);
    void chooseBars (bool on);
    void chooseBarCount (int index);
    void togglePause();
    void chooseView (std::vector<int> channels);
    void chooseCompare (int a, int b);
    void chooseClash (bool on);
    /* The view, the comparison and the clash, with the buses they need. */
    void sendLook (const std::vector<int>& view, int a, int b, bool clash);

    Model& model;
    Shown shown;

    bool live = false;
    double lastColumnMs = 0.0;
    bool paused = false;
    bool barView = false;
    int barIndex = defaultBarCount;

    /* One take's columns, sized once for the largest the plugin hands over. */
    std::vector<std::uint8_t> levels, clashLevels;
    std::array<int, Model::maxColumns> slots {};

    Words noSignal { Words::Voice::warning };
    ni::ui::Select range, barCount;
    ni::ui::Toggle barsToggle { "bars" };
    ni::ui::Button pause { {}, "pause" };

    Words viewCaption { Words::Voice::caption, "view" };
    ni::ui::CheckList view;
    Divider divider;
    Words compareCaption { Words::Voice::caption, "compare" };
    ni::ui::Select compareA, compareB;
    Words vs { Words::Voice::quiet, "vs" };
    ni::ui::Toggle clash { "clash" };

    FrequencyScale scale;
    Well pictureWell;
    TimeAxis axis;
    CrosshairReadout reading;

    JUCE_DECLARE_NON_COPYABLE (SpectrogramView)
};

/* The whole editor: the frame, the view in it, scaled to whatever the host
 * gives. What the plugin's AudioProcessorEditor shows. */
class SpectrogramEditor final : public juce::Component
{
public:
    static constexpr int designWidth = 720;
    static constexpr int designHeight = ni::ui::EditorFrame::heightFor (SpectrogramView::height);

    /* `model` must outlive the editor. `clock` is the frame's (a test's). */
    explicit SpectrogramEditor (Model& model, ni::ui::EditorFrame::Clock clock = {});
    ~SpectrogramEditor() override;

    ni::ui::EditorFrame& frame() noexcept { return window; }
    SpectrogramView& view() noexcept { return content; }
    ni::ui::FixedDesign& fit() noexcept { return scaled; }

    void resized() override;

private:
    ni::ui::EditorFrame window;
    SpectrogramView content;
    ni::ui::FixedDesign scaled { window, designWidth, designHeight };
    ni::ui::FrameClock::Subscription ticking;

    JUCE_DECLARE_NON_COPYABLE (SpectrogramEditor)
};

} // namespace ni::spectrogram

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

#include "SpectrogramEditor.h"

#include "Channels.h"
#include "ChildLights.h"
#include "InfoLines.h"
#include "UvTokens.h"

#include <algorithm>

namespace ni::spectrogram
{

namespace
{
constexpr int controlH = (int) uv::tok::size::controlH;
constexpr int s1 = (int) uv::tok::space::space1;
constexpr int s2 = (int) uv::tok::space::space2;
constexpr int s3 = (int) uv::tok::space::space3;
constexpr int s4 = (int) uv::tok::space::space4;

/* The rows, from the top of the content (SpectrogramEditor.h has the sum). */
constexpr int stripY = controlH + s4;
constexpr int displayY = stripY + controlH + s4;
constexpr int wellH = pictureHeight + 2;
constexpr int axisY = displayY + wellH + s2;
constexpr int axisH = 16;
constexpr int readoutY = axisY + axisH + s3;
constexpr int readoutH = 20;
static_assert (readoutY + readoutH == SpectrogramView::height);

/* Across: the frequency gutter, space-2, the well; the picture is a hairline
 * inside it, and everything under the well lines up with the picture. */
constexpr int scaleW = 40;
constexpr int wellX = scaleW + s2;
constexpr int pictureX = wellX + 1;
static_assert (wellX + pictureWidth + 2 == SpectrogramView::width);

/* The field widths the web editor gave its controls. */
constexpr int rangeW = 96;
constexpr int barCountW = 64;
constexpr int viewW = 150;
constexpr int compareW = 112;

bool sameSession (const Session& a, const Session& b)
{
    return juce::exactlyEqual (a.rangeLo, b.rangeLo) && juce::exactlyEqual (a.rangeHi, b.rangeHi)
        && a.view == b.view && a.compareA == b.compareA && a.compareB == b.compareB
        && a.clash == b.clash;
}
} // namespace

/* ------------------------------------------------------------------- view */

SpectrogramView::SpectrogramView (Model& m)
    : model (m),
      levels ((size_t) Model::maxColumns * ni::ui::Spectrogram::defaultBands),
      clashLevels (levels.size())
{

    /* THE TOOLBAR: how the picture is drawn. */
    addAndMakeVisible (noSignal);

    juce::StringArray rangeNames;
    for (const auto& r : ranges)
        rangeNames.add (r.name);
    range.setOptions (rangeNames);
    range.setFieldWidth (rangeW);
    range.setTitle ("Range");
    ni::ui::setInfo (range, info::range);
    range.onChange = [this] (int i) { chooseRange (i); };
    addAndMakeVisible (range);

    /* The switch says WHICH axis; the select says how much of it, and stays
     * while the switch is off, so the window does not change shape. */
    ni::ui::setInfo (barsToggle, info::bars);
    barsToggle.onChange = [this] (bool on) { chooseBars (on); };
    addAndMakeVisible (barsToggle);

    juce::StringArray counts;
    for (int n : barCounts)
        counts.add (juce::String (n));
    barCount.setOptions (counts);
    barCount.setIndex (barIndex);
    barCount.setFieldWidth (barCountW);
    barCount.setTitle ("Bars shown");
    ni::ui::setInfo (barCount, info::barCount);
    barCount.onChange = [this] (int i) { chooseBarCount (i); };
    addAndMakeVisible (barCount);

    /* The transport's pause glyph, latched and lit while the picture is held. */
    pause.setOn (false);
    pause.setTitle (info::pauseTitle);
    ni::ui::setInfo (pause, info::pause);
    pause.onClick = [this] { togglePause(); };
    addAndMakeVisible (pause);

    /* THE SOURCE STRIP: two groups and a rule, because "what is the picture
     * of" and "what is the clash measuring" are two questions. */
    addAndMakeVisible (viewCaption);
    view.setFaceWidth (viewW);
    view.setEmptyText ("no Listen-In found");
    view.setTitle ("View");
    ni::ui::setInfo (view, info::view);
    view.onChange = [this] (std::vector<int> ids) { chooseView (std::move (ids)); };
    addAndMakeVisible (view);

    addAndMakeVisible (divider);

    addAndMakeVisible (compareCaption);
    compareA.setFieldWidth (compareW);
    compareA.setTitle ("Compare");
    ni::ui::setInfo (compareA, info::compare);
    compareA.onChange = [this] (int i) { chooseCompare (i, model.session().compareB); };
    addAndMakeVisible (compareA);

    addAndMakeVisible (vs);

    compareB.setFieldWidth (compareW);
    compareB.setTitle ("Against");
    ni::ui::setInfo (compareB, info::against);
    compareB.onChange = [this] (int i) { chooseCompare (model.session().compareA, i); };
    addAndMakeVisible (compareB);

    ni::ui::setInfo (clash, info::clash);
    clash.onChange = [this] (bool on) { chooseClash (on); };
    addAndMakeVisible (clash);

    /* THE DISPLAY. */
    addAndMakeVisible (scale);
    ni::ui::setInfo (picture(), info::picture);
    picture().onHover = [this] (const std::optional<ni::ui::Spectrogram::Sample>& s) { refreshReadout (s); };
    addAndMakeVisible (pictureWell);
    addAndMakeVisible (axis);
    addAndMakeVisible (reading);

    /* What the session holds, before anything is shown: an editor opens on
     * the session, never on defaults of its own. */
    syncTransport (true);
    syncAxis (true);
    syncSources (true);
    syncSession (true);
    refreshTimeMarks();
    noSignal.setText ("no signal");

    /* Last: the layout reads the controls' configured widths. */
    setSize (width, height);
}

SpectrogramView::~SpectrogramView()
{
    picture().onHover = nullptr;
}

int SpectrogramView::bars() const
{
    return barCounts[juce::jlimit (0, numBarCounts - 1, barIndex)];
}

/* ---------------------------------------------------------------- the tick */

void SpectrogramView::update (double nowMs)
{
    syncTransport (false);
    syncAxis (false);
    syncSources (false);
    syncSession (false);
    takeColumns (nowMs);

    /* Columns stop when the host stops processing: saying so beats a frozen
     * picture that looks like a crash. */
    if (live && nowMs - lastColumnMs > stallMs)
        live = false;
    noSignal.setText (live ? juce::String() : juce::String ("no signal"));
}

/*
 * WHERE EACH COLUMN BELONGS IN THE BAR VIEW is decided here, not by the
 * plugin: a slot per column, BACK-DATED FROM THE NEWEST, so a catch-up batch
 * covers the musical ground it really spans instead of stacking on one
 * pixel. Not gated on pause: the freeze is the picture's repaint gate.
 */
void SpectrogramView::takeColumns (double nowMs)
{
    const int bands = (int) shown.hz.size();
    if (bands <= 0 || bands > ni::ui::Spectrogram::defaultBands)
        return;

    int clashCount = 0;
    const int n = juce::jmin (Model::maxColumns,
                              model.takeColumns (levels.data(), clashLevels.data(), Model::maxColumns, clashCount));
    if (n <= 0)
        return;

    const auto& t = shown.transport;
    for (int c = 0; c < n; ++c)
        slots[(size_t) c] = slotForPpq (t.ppq - (n - 1 - c) * t.ppqPerColumn, bars(), t.numerator,
                                        t.denominator, pictureWidth);

    /* The mask belongs to these columns only if it has their shape. */
    picture().push ({ levels.data(), n, bands, slots.data(), clashCount == n ? clashLevels.data() : nullptr });

    live = true;
    lastColumnMs = nowMs;
}

void SpectrogramView::syncTransport (bool force)
{
    const auto t = model.transport();
    if (! force && t == shown.transport)
        return;
    const bool metre = t.numerator != shown.transport.numerator || t.denominator != shown.transport.denominator;
    const bool rate = t.sampleRate != shown.transport.sampleRate;
    shown.transport = t;
    if (metre && barView)
        refreshTimeMarks();
    if (rate && ! force)
        syncSources (true);
}

void SpectrogramView::syncAxis (bool force)
{
    const auto& hz = model.bandCentres();
    if (! force && hz == shown.hz)
        return;
    shown.hz = hz;
    scale.setMarks (freqMarks (shown.hz, (float) pictureHeight));
    refreshReadout (picture().hovered());
}

/* A bus appears at human speed -- somebody inserts a Listen-In -- and the
 * lists follow. A bus at another rate is listed and refused, never hidden. */
void SpectrogramView::syncSources (bool force)
{
    const auto& sources = model.sources();
    if (! force && sources == shown.sources)
        return;
    shown.sources = sources;

    const auto names = channelNames (shown.sources);
    view.setOptions (viewOptions (shown.sources, referenceRate (shown.transport, shown.sources)));
    compareA.setOptions (names);
    compareB.setOptions (names);
    /* The options moved under the session's indices: show them again. */
    syncSession (true);
}

void SpectrogramView::syncSession (bool force)
{
    const auto& s = model.session();
    if (! force && sameSession (s, shown.session))
        return;

    /* A new mix or a new scale makes the history an answer about something
     * else -- whether the user changed it or the host loaded a session. */
    if (shown.any && (s.view != shown.session.view || ! juce::exactlyEqual (s.rangeLo, shown.session.rangeLo)
                      || ! juce::exactlyEqual (s.rangeHi, shown.session.rangeHi)))
        picture().clear();

    shown.session = s;
    shown.any = true;

    range.setIndex (rangeIndex (s.rangeLo, s.rangeHi));
    view.setSelected (s.view);
    view.setSummary (viewSummary (s.view, channelNames (shown.sources)));
    compareA.setIndex (s.compareA);
    compareB.setIndex (s.compareB);
    clash.setOn (s.clash);
    picture().setClash (s.clash);
}

void SpectrogramView::refreshTimeMarks()
{
    const auto& t = shown.transport;
    axis.setMarks (barView ? barMarks (bars(), t.numerator, t.denominator, (float) pictureWidth)
                           : secondMarks (pictureWidth, columnsPerSecond, (float) pictureWidth),
                   barView);
}

/* The three readouts say a dash with the pointer away, not a stale number. */
void SpectrogramView::refreshReadout (const std::optional<ni::ui::Spectrogram::Sample>& s)
{
    const juce::String key = barView ? "pos" : "time";
    if (! s.has_value())
    {
        reading.setValues (noReading(), key, noReading(), noReading());
        return;
    }
    const auto& t = shown.transport;
    reading.setValues (readFrequency (shown.hz, s->band), key,
                       barView ? readPosition (s->slot, bars(), t.numerator, t.denominator) : readAge (s->age),
                       readLevel (s->level));
}

/* ------------------------------------------------------------ the choices */

/* The zoom. The plugin re-bands and sends the axis back; the picture clears
 * and unpauses, because a frozen picture of a range just left is a lie. */
void SpectrogramView::chooseRange (int index)
{
    if (index < 0 || index >= numRanges)
        return;
    if (paused)
        togglePause();
    picture().clear();
    model.setRange (ranges[index].lo, ranges[index].hi);
    syncSession (false);
}

void SpectrogramView::chooseBars (bool on)
{
    barView = on;
    barsToggle.setOn (on);
    /* Both pictures were written from every column: switching loses nothing. */
    picture().setView (on ? ni::ui::Spectrogram::View::bars : ni::ui::Spectrogram::View::scroll);
    refreshTimeMarks();
    refreshReadout (picture().hovered());
}

void SpectrogramView::chooseBarCount (int index)
{
    barIndex = juce::jlimit (0, numBarCounts - 1, index);
    barCount.setIndex (barIndex);
    refreshTimeMarks();
    refreshReadout (picture().hovered());
}

/* PAUSE NEVER LEAVES THE EDITOR: the analysis runs on, only the repaint stops. */
void SpectrogramView::togglePause()
{
    paused = ! paused;
    picture().setPaused (paused);
    pause.setOn (paused);
    pause.setTitle (paused ? info::resumeTitle : info::pauseTitle);
}

/* Never nothing: a spectrogram showing no channel is a broken plugin. */
void SpectrogramView::chooseView (std::vector<int> channels)
{
    std::sort (channels.begin(), channels.end());
    channels.erase (std::unique (channels.begin(), channels.end()), channels.end());
    if (channels.empty())
        channels.push_back (0);
    const auto& s = model.session();
    sendLook (channels, s.compareA, s.compareB, s.clash);
}

void SpectrogramView::chooseCompare (int a, int b)
{
    const auto& s = model.session();
    sendLook (s.view, a, b, s.clash);
}

void SpectrogramView::chooseClash (bool on)
{
    model.setClashCriteria (clashFloorDb, clashBalanceDb);
    const auto& s = model.session();
    sendLook (s.view, s.compareA, s.compareB, on);
}

void SpectrogramView::sendLook (const std::vector<int>& v, int a, int b, bool on)
{
    /* Copied first: the model's session is what these are read from. */
    const std::vector<int> viewCopy (v);
    model.setLook (viewCopy, a, b, on, listenSlots (viewCopy, a, b, on, shown.sources));
    syncSession (false);
}

/* ----------------------------------------------------------------- layout */

void SpectrogramView::resized()
{
    /* THE TOOLBAR: the amber word on the left, the actions stacked on the
     * right edge, space-4 apart. */
    noSignal.setBounds (0, 0, width / 2, controlH);
    int x = width;
    const auto right = [&x] (juce::Component& c, int w) {
        x -= w;
        c.setBounds (x, 0, w, controlH);
        x -= s4;
    };
    right (pause, controlH);
    right (barCount, barCount.idealWidth());
    right (barsToggle, barsToggle.idealWidth());
    right (range, range.idealWidth());

    /* THE STRIP: space-between, space-2 apart, the rule inset space-2 either
     * side of it and the "vs" space-1 either side. */
    const int captionW = viewCaption.idealWidth();
    viewCaption.setBounds (0, stripY, captionW, controlH);
    view.setBounds (captionW + s2, stripY, view.idealWidth(), controlH);
    const int leftEnd = view.getRight();

    x = width;
    const auto fromRight = [&x] (juce::Component& c, int w, int before) {
        x -= w;
        c.setBounds (x, stripY, w, controlH);
        x -= before;
    };
    fromRight (clash, clash.idealWidth(), s2);
    fromRight (compareB, compareB.idealWidth(), s2 + s1);
    fromRight (vs, vs.idealWidth(), s1 + s2);
    fromRight (compareA, compareA.idealWidth(), s2);
    fromRight (compareCaption, compareCaption.idealWidth(), 0);
    const int rightStart = x;

    /* The rule's box is its hairline and space-2 either side; the free space
     * is shared on either side of it. */
    const int ruleBox = 1 + 2 * s2;
    const int free = rightStart - leftEnd - ruleBox - 2 * s2;
    divider.setBounds (leftEnd + s2 + free / 2 + s2, stripY, 1, controlH);

    /* THE DISPLAY, and under it the axis and the readout on the picture's x. */
    scale.setBounds (0, displayY - FrequencyScale::overhang, scaleW, wellH + 2 * FrequencyScale::overhang);
    pictureWell.setBounds (wellX, displayY, pictureWidth + 2, wellH);
    /* The axis's labels hang under its 16 px; it is given the space to the
     * readout so they are not cut. */
    axis.setBounds (pictureX - TimeAxis::overhang, axisY, pictureWidth + 2 * TimeAxis::overhang, readoutY - axisY);
    reading.setBounds (pictureX, readoutY, width - pictureX, readoutH);
}

void SpectrogramView::paint (juce::Graphics& g)
{
    ni::ui::paintChildLights (g, *this);
}

void SpectrogramView::paintLight (juce::Graphics& g)
{
    ni::ui::forwardChildLights (g, *this);
}

/* ----------------------------------------------------------------- editor */

SpectrogramEditor::SpectrogramEditor (Model& m, ni::ui::EditorFrame::Clock clock)
    : window (m, std::move (clock)),
      content (m)
{
    window.setSize (designWidth, designHeight);
    window.setContent (&content);
    /* Conventions only, as the Hint card has the bar. */
    window.setConventions ({ { "hover", "to read a point" }, { "clash", "marks where channels collide" } });
    window.setMotionInfo (info::motion);
    window.setSignatureInfo (info::signature);

    addAndMakeVisible (scaled);
    ticking = window.frameClock().subscribe ([this] (double now) { content.update (now); });
    setSize (designWidth, designHeight);
}

SpectrogramEditor::~SpectrogramEditor()
{
    ticking.reset();
    window.setContent (nullptr);
}

void SpectrogramEditor::resized()
{
    scaled.setBounds (getLocalBounds());
}

} // namespace ni::spectrogram

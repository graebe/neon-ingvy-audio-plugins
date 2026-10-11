// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What NI Spectrogram's editor needs from its plugin, and all it may ask of
 * it: the picture's columns as the analyzer finishes them, the axis they were
 * measured on, the host's clock, the Listen-In buses that exist, and the
 * session -- what the window is looking at -- with the commands that change it.
 *
 * NO PARAMETERS. Nothing here changes what comes out of the plugin, Pause
 * included; numParameters() is 0 and parameter() is never called. What the
 * user sets up is the SESSION, saved with the host's state (the chunk the
 * iPlug2 build wrote, tests/fixtures/iplug2/FORMAT.md on the spike branch:
 * the buses, the clash criteria, the view, the comparison, the range).
 *
 * WHAT THIS REPLACES. The web editor's message tags (ui/src/lib/msg.js and
 * EMsgTags in Spectrogram.h), one call per tag, with the strings gone:
 *
 *   kMsgCols, kMsgClashCols   takeColumns: levels and clash of the SAME
 *                             columns, so the mask can never be a frame
 *                             apart from the picture under it
 *   kMsgAxis                  bandCentres
 *   kMsgSync                  transport
 *   kMsgSources               sources
 *   kMsgState                 session, readable at any time -- so there is
 *                             no push gate: an editor opens on what the
 *                             session holds, and cannot overwrite it with
 *                             defaults it never had
 *   kMsgRange                 setRange
 *   kMsgView, kMsgCompare,    setLook, one command for the three, which the
 *   kMsgSelect                web editor always sent together
 *   kMsgClash                 setClashCriteria
 *
 * MESSAGE THREAD, EVERY CALL, as EditorModel says. A getter returns the
 * latest snapshot already copied out; the references stay valid until the
 * next call into the model. session() holds an edit the moment the command
 * returns (the plugin's Session::Get, which includes a change the receiver has
 * not adopted yet), so a control never shows the old choice for a frame; the
 * axis a new range makes arrives later, in bandCentres().
 */
#pragma once

#include "BusSource.h"
#include "EditorModel.h"

#include <cstdint>
#include <vector>

namespace ni::spectrogram
{

/* The host's clock, as the plugin last saw it on the audio thread. */
struct Transport
{
    /* Quarter notes. While the transport is stopped (or the host reports no
     * beat timeline) it advances at the last tempo seen, so the bar view
     * keeps filling. */
    double ppq = 0.0;
    double bpm = 120.0;
    int numerator = 4;
    int denominator = 4;
    /* The host's playhead is the authority; false is the bar view's `free`. */
    bool running = false;
    /* Quarter notes one column covers: hop / rate * bpm / 60. A catch-up
     * batch is spread over the positions it really spans with it. 0 before
     * the plugin knows its rate. */
    double ppqPerColumn = 0.0;
    /* The session's sample rate, 0 before it is known. */
    int sampleRate = 0;

    bool operator== (const Transport& o) const
    {
        return juce::exactlyEqual (ppq, o.ppq) && juce::exactlyEqual (bpm, o.bpm)
            && numerator == o.numerator && denominator == o.denominator && running == o.running
            && juce::exactlyEqual (ppqPerColumn, o.ppqPerColumn) && sampleRate == o.sampleRate;
    }
};

/* A Listen-In bus that exists, sending or not: the kit's (BusSource.h). */
using Source = ni::ui::BusSource;

/*
 * What the session is looking at. A CHANNEL is an index into the editor's
 * channel list: 0 is the track the plugin sits on, n > 0 the n-th live bus in
 * slot order -- the order the plugin opens them in.
 */
struct Session
{
    /* The zoom, in Hz. */
    float rangeLo = 10.0f;
    float rangeHi = 20000.0f;
    /* The channels ADDED into the picture, ascending, never empty. */
    std::vector<int> view { 0 };
    /* The two channels the clash is measured between, and whether it is. */
    int compareA = 0;
    int compareB = 1;
    bool clash = false;
    /* What counts as a clash: both above the floor, within the balance. */
    float clashFloorDb = -60.0f;
    float clashBalanceDb = 12.0f;
    /* The bus slots the plugin opens, derived from the view and the
     * comparison by the editor (Channels.h). */
    std::vector<int> listen;
};

class Model : public ni::ui::EditorModel
{
public:
    /* The most columns one takeColumns hands over: the plugin's catch-up
     * bound per tick. */
    static constexpr int maxColumns = 32;

    /*
     * The columns finished since the last call, oldest first, at most
     * `capacity`: `bands` level bytes each (bandCentres().size()), band 0 the
     * lowest, written to `levels` -- the view's channels summed in power by
     * the engine. When the clash is on, `clash` gets the mask measured for
     * the same columns, in the same shape, and `clashCount` says how many
     * (that count or 0). Returns the number of columns.
     *
     * Columns measured against an axis other than bandCentres() are never
     * handed over: a range change drops them.
     */
    virtual int takeColumns (std::uint8_t* levels, std::uint8_t* clash, int capacity,
                             int& clashCount) = 0;

    /* The band centre frequencies, in Hz, band 0 first: the analyzer's own
     * log mapping, from which every label on the scale is placed. Empty
     * before the first configuration. */
    virtual const std::vector<float>& bandCentres() const = 0;

    virtual Transport transport() const = 0;

    /* Every bus that exists, in slot order. */
    virtual const std::vector<Source>& sources() const = 0;

    virtual const Session& session() const = 0;

    /* ---- commands */

    /* The zoom. The engine refuses an undrawable range and clamps the top to
     * Nyquist; bandCentres() says what it made of it. */
    virtual void setRange (float lo, float hi) = 0;

    /* What the picture is of, what the clash measures and whether it is
     * wanted, and the buses to open for both. An empty view is the own
     * channel alone. */
    virtual void setLook (const std::vector<int>& view, int compareA, int compareB, bool clash,
                          const std::vector<int>& listen) = 0;

    virtual void setClashCriteria (float floorDb, float balanceDb) = 0;
};

} // namespace ni::spectrogram

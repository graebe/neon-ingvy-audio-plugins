// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What NI Listen-In's editor needs from its plugin, and all it may ask.
 *
 * The web editor was fed through two message tags (ui/src/lib/msg.js):
 * kMsgState, "<slot>:<status>:<peak>" every idle tick, and kMsgLabel, the bus
 * name both ways. This is the same traffic as C++ -- a status, a peak and a
 * name to read, and a name to write -- with no tag, no text where the data is
 * a number and no format to parse. The slot is not here: it is the host
 * parameter, and the editor reads and writes it as one (parameter (param::bus)).
 *
 * MESSAGE THREAD, EVERY CALL (EditorModel.h). The getters return what is
 * current and never block: the status is the main thread's own (ListenIn.cpp's
 * mStatus, set where the bus is claimed), the peak an atomic the audio thread
 * writes, the name the session's (listenin::state::Session) copied out under
 * its lock.
 *
 * THE NAME IS THE ONE COMMAND, AND IT IS SYNCHRONOUS. setLabel() hands the
 * plugin what was typed; the plugin keeps it sanitised -- control characters
 * and colons dropped, cut to maxLabelBytes on a UTF-8 boundary
 * (listenin::wire::parse_label) -- and label() returns the kept name from the
 * moment setLabel() returns, as the session holds it at once. That is what lets
 * the field show what the plugin KEPT rather than what was typed, without a
 * frame of the old name in between. Publishing it to the bus is the plugin's
 * business, on its next idle tick, and the editor never waits for it.
 *
 * The processor implements this; tests/ui/listen-in_fakes.h implements it
 * with nothing behind it.
 */
#pragma once

#include "EditorModel.h"

namespace ni::li
{

/* The host parameters' indices: the iPlug2 order (State.h's EParams), which
 * is their VST3 ID -- tests/fixtures/iplug2/NIListenIn/parameters.json on the
 * spike branch lists Bus as ID 0. Bypass is the host's and is not among them. */
namespace param
{
enum : int
{
    bus = 0,   // "Bus", a stepped integer 1..numBuses, default 1; its text is "3"
    count
};
} // namespace param

/* The buses: ABUS_MAX_SLOT, the range of the Bus parameter. */
inline constexpr int numBuses = 16;

/* The longest name the plugin keeps, in UTF-8 bytes: the bus label's 32 less
 * its NUL. The field's own limit is this many characters -- a courtesy, as
 * the web field's maxlength was; the plugin's byte count is the rule. */
inline constexpr int maxLabelBytes = 31;

/* What the bus is doing: listenin::wire::Status, value for value. */
enum class Status : int
{
    idle = 0,          // no slot claimed yet: the host has not started audio
    live = 1,          // publishing on the bus
    taken = 2,         // another NI Listen-In holds this bus
    unavailable = 3,   // the bus could not be made or mapped (a sandboxed host)
};

class Model : public ni::ui::EditorModel
{
public:
    /* The bus's state, as the plugin last decided it. */
    virtual Status status() const = 0;

    /* The input's peak, linear, 0..1 -- clamped by the plugin, and decaying
     * there rather than reset per block, so a quiet block read between two
     * loud ones does not flicker the meter. The editor maps it to the meter's
     * dB scale (ListenInEditor.h). */
    virtual float peak() const = 0;

    /* The bus's name, as the plugin keeps it; empty for none. */
    virtual juce::String label() const = 0;

    /* A name typed in the editor, once per edit. label() is the kept one from
     * the moment this returns. */
    virtual void setLabel (const juce::String& typed) = 0;
};

} // namespace ni::li

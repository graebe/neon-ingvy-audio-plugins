// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In's host parameter, its saved state, and the bus name between
 * threads: plain C++, no JUCE, so the state's tests link it on its own.
 *
 * ONE PARAMETER, AND IT EARNS ITS PLACE. The audio is passed through bit for
 * bit whatever the bus says, but a bus NUMBER is a piece of session structure
 * the host should own: it belongs in the saved set, it survives a reopen, and
 * somebody will want to automate a switch between two sources. The host does
 * all of that for a parameter and none of it for anything else.
 *
 * THE SAME PARAMETER AS THE iPlug2 BUILD, OR ITS SETS DO NOT OPEN: "Bus", a
 * stepped integer 1..16, default 1, at index 0 -- with legacy parameter IDs
 * the index is the VST3 ID. tests/fixtures/iplug2/NIListenIn/parameters.json
 * is what the host saw, and the processor's tests hold this table to it.
 */
#pragma once

#include "Nist.h"
#include "ParamSpec.h"

#include <mutex>
#include <string>

namespace ni::li
{

enum Param : int
{
    kBus = 0,
    kNumParams
};

/* The bus parameter's row, and the table it is the whole of. */
const ParamSpec& busSpec();

/*
 * THE SAVED STATE: FORMAT.md's Listen-In -- the one parameter, then the bus
 * name as a required string (it may be empty). Every headerless chunk an
 * earlier build wrote has the same one parameter.
 */
const nist::Layout& layout();

/*
 * THE NAME, BETWEEN THREADS.
 *
 * A host saves and loads state on a thread of its choosing, while the editor
 * and the bus service run on the message thread, which alone owns the bus
 * writer. So the name is not a plain member any of them writes: a load records
 * it here, the editor edits it here, and the message thread takes what changed
 * and hands it to the writer. A save reads it here, so a save straight after a
 * load writes the load.
 *
 *   any thread but audio     label, load, edit
 *   message thread           take
 *
 * The lock is held only to copy the name in or out -- never across a bus or
 * editor call, never on the audio thread. Every name given is already kept
 * (wire::parse_label).
 */
class Session
{
public:
    /* What a save writes: the name, a load not yet taken included. */
    std::string label() const;
    /* A state load's name. The next take reports it as a load. */
    void load (std::string label);
    /* A name typed in the editor. */
    void edit (std::string label);

    /* Message thread. What changed since the last take: false when nothing
     * did. `loaded` says a state load did it, after which the bus is claimed
     * afresh. */
    bool take (std::string& label, bool& loaded);

private:
    mutable std::mutex lock;
    std::string name;     /* under lock */
    bool changed = false; /* under lock */
    bool loaded = false;  /* under lock */
};

} // namespace ni::li

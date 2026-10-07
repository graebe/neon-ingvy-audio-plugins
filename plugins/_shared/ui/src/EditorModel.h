// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What every editor gets from its plugin, whatever the product: the host
 * parameters, the Ground's rings and the Motion switch's memory.
 *
 * AN EDITOR TALKS TO ITS PLUGIN THROUGH A MODEL AND NOTHING ELSE. Each product
 * has plugins/<product>/editor/Model.h, a class derived from this one that
 * adds the product's own data -- snapshot getters (the pattern, the playhead,
 * a scope, a list of buses) and commands (toggle a step, copy a slot, name a
 * bus) -- in plain C++ types: no message tags, no strings where the data is a
 * number, no JUCE ValueTree as a wire. The processor implements it, reading
 * the engine's triple-buffered snapshots and posting to its command queue;
 * the tests implement it with fakes. docs/tech/native-ui.md has the whole
 * contract.
 *
 * MESSAGE THREAD, EVERY CALL. A getter returns the latest snapshot the engine
 * published, already copied out: cheap, never blocking, never torn. A command
 * returns at once; its effect shows in a later snapshot. Nothing here is a
 * callback the audio thread makes: the editor asks, on its FrameClock tick
 * or in a paint, and the model answers with what is current.
 */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ni::ui
{

class EditorModel
{
public:
    virtual ~EditorModel() = default;

    /*
     * The host parameters, in their VST3 ID order -- which is the iPlug2
     * builds' index, kept so the sets they saved open with the same settings
     * (tests/fixtures/iplug2 on the spike branch lists them). Bypass is the
     * host's and is not among them. A control binds one with ParamBinding.
     */
    virtual int numParameters() const = 0;
    virtual juce::RangedAudioParameter& parameter (int index) = 0;

    /*
     * The Ground's rings since the last call, oldest first, at most
     * `capacity`: one strength per ring, 1 on a bar's downbeat and
     * uv::tok::motion::beat::beat on every other quarter note. The plugin
     * counts them from the host's transport, on the audio thread, where the
     * position is; the editor only plays them. Returns how many it wrote.
     */
    virtual int takeRings (float* strengths, int capacity) = 0;

    /*
     * The Motion switch: whether the Ground moves. Remembered per machine and
     * per editor, never in the host's state -- whether a background animates
     * is a view, not a setting a session should carry to someone else's
     * computer (the web kit's lib/motion.js has the argument). On by default.
     */
    virtual bool motion() const = 0;
    virtual void setMotion (bool on) = 0;
};

} // namespace ni::ui

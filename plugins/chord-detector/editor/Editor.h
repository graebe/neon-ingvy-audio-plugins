// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Chord-Detector's window, 760 x 616 at its design size: the board "NI
 * Chord-Detector — proposed" on the NI Plugin Layouts canvas
 * (design/designs/ChordDetector-Proposed.dc.html), built from the kit.
 *
 *   (32, 32)   the Circle of Fifths, 232 square, KEY and the Mode select in
 *              its centre, the key signature under them
 *   (296, 32)  the Chord Readout, then SPELLING (Auto, #, b), HOLD and the
 *              PEDAL LED along the bottom of the column
 *   (32, 288)  HISTORY, then Zoom, Span and the Staff / MIDI switch, over a
 *              136px well holding the Grand Staff or the Piano Roll
 *   (32, 476)  the Keyboard, in a 72px well
 *   (0, 572)   the Hint: three conventions, then the Signature
 *
 * EVERYTHING SHOWN COMES FROM THE MODEL, once a frame: the reading when its
 * serial moves, the note events into the history every frame. The events are
 * a stream the editor only hears while it is open, so the history starts from
 * what is sounding when the window opens, and is put right again from what is
 * sounding whenever the engine reports events it had to drop. Every control
 * is a host parameter, bound through ParamBinding; the window keeps nothing a
 * session would miss. It draws at its design size: the host's window around it
 * scales it by Zoom (ni::PluginEditor).
 */
#pragma once

#include "Button.h"
#include "ChordReadout.h"
#include "CircleOfFifths.h"
#include "FrameClock.h"
#include "GrandStaff.h"
#include "Hint.h"
#include "Info.h"
#include "Keyboard.h"
#include "Model.h"
#include "NoteHistory.h"
#include "ParamBinding.h"
#include "PianoRoll.h"
#include "Select.h"
#include "Toggle.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace ni::chord_detector
{

class Editor final : public juce::Component,
                     public ni::ui::InfoHost
{
public:
    static constexpr int width = 760;
    static constexpr int height = 616;


    explicit Editor (Model&);
    ~Editor() override;

    /* One frame: what the clock calls, and what a test calls. */
    void tick();

    ni::ui::InfoState& infoState() override { return info; }

    void paint (juce::Graphics&) override;
    void resized() override;

    /* The parts, as the window holds them: for the tests. */
    ni::ui::CircleOfFifths& circleOfFifths() noexcept { return circle; }
    ni::ui::ChordReadout& chordReadout() noexcept { return readout; }
    ni::ui::Keyboard& keyboardView() noexcept { return keyboard; }
    ni::ui::GrandStaff& grandStaff() noexcept { return staff; }
    ni::ui::PianoRoll& pianoRoll() noexcept { return roll; }
    const ni::ui::NoteHistory& noteHistory() const noexcept { return history; }
    ni::ui::Button& spellingButton (int choice) noexcept { return spellings[(size_t) choice]; }
    ni::ui::Button& viewButton (int choice) noexcept { return views[(size_t) choice]; }
    ni::ui::Toggle& holdSwitch() noexcept { return holdToggle; }
    ni::ui::Select& modeSelect() noexcept { return modeBox; }
    ni::ui::Select& spanSelect() noexcept { return spanBox; }
    ni::ui::Select& zoomSelect() noexcept { return zoomBox; }
    ni::ui::Hint& hint() noexcept { return bar; }

private:
    ni::ui::ParamBinding& binding (Param p) { return *bindings[(size_t) p]; }
    int choiceOf (Param) const;
    void commit (Param, int choice);
    void bind (ni::ui::Select&, Param);
    void showParameters();
    void showReading (const CdReading&);
    void drainNotes();
    void reconcile (const CdReading&);
    double spanQuarters (const CdReading&) const;

    Model& model;

    ni::ui::InfoState info;
    ni::ui::InfoTracker tracker { *this, info };
    ni::ui::FrameClock clock { *this };
    ni::ui::FrameClock::Subscription frame;

    std::array<std::unique_ptr<ni::ui::ParamBinding>, paramCount> bindings;

    ni::ui::CircleOfFifths circle;
    ni::ui::Select modeBox;
    ni::ui::ChordReadout readout;
    std::array<ni::ui::Button, 3> spellings;
    ni::ui::ButtonGroup spellingGroup;
    ni::ui::Toggle holdToggle { "Hold" };
    ni::ui::Select zoomBox;
    ni::ui::Select spanBox;
    std::array<ni::ui::Button, 2> views;
    ni::ui::ButtonGroup viewGroup;
    ni::ui::NoteHistory history;
    ni::ui::GrandStaff staff { history };
    ni::ui::PianoRoll roll { history };
    ni::ui::Keyboard keyboard;
    ni::ui::Hint bar;

    std::uint32_t shownSerial = 0xffffffffu;
    std::uint32_t seenDropped = 0;
    bool pedal = false;

    JUCE_DECLARE_NON_COPYABLE (Editor)
};

} // namespace ni::chord_detector

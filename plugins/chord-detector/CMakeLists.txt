# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Chord-Detector: a silent VST3 instrument that names what is played into
# it. The engine is engines/chord-detector (cd_engine); the editor is
# editor/, built on the kit.
ni_add_juce_plugin(chord-detector
    TARGET NIChordDetector
    NAME "NI Chord-Detector"
    CODE ChDt
    ENGINE cd_engine
    CATEGORIES Instrument Tools
    SYNTH
    MIDI_IN
    EDITOR
    # The editor embeds JetBrains Mono and Bravura: both OFLs travel with it.
    NOTICES ${NI_UI_FONT_LICENSE} ${NI_UI_MUSIC_FONT_LICENSE}
    SOURCES ChordDetector.cpp)

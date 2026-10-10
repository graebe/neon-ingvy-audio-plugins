# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Vocalize: any sound, more and more a voice singing A, E, I, O or U. The
# engine is engines/vocalize (vz_engine); the editor is editor/, built on the
# kit. No MIDI in: on the Move the panic arrives as MIDI, in Live as a
# reactivation (Vocalize.h).
ni_add_juce_plugin(vocalize
    TARGET NIVocalize
    NAME "NI Vocalize"
    CODE Vclz
    ENGINE vz_engine
    CATEGORIES Fx Filter
    EDITOR
    SOURCES Vocalize.cpp Vocalize.h)

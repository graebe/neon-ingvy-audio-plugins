# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Bit-Crusher: a continuous signal made discrete, in amplitude and in time.
# The engine is engines/bit-crusher (bc_engine); the editor is editor/, built
# on the kit. No MIDI in: on the Move the panic arrives as MIDI, in Live as a
# reactivation (BitCrusher.h).
ni_add_juce_plugin(bit-crusher
    TARGET NIBitCrusher
    NAME "NI Bit-Crusher"
    CODE BtCr
    ENGINE bc_engine
    CATEGORIES Fx Distortion
    EDITOR
    SOURCES BitCrusher.cpp BitCrusher.h)

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Filter Designer: a filter design tool you play as an equaliser. The
# engine is engines/filter-designer (fd_engine); the editor is editor/, built
# on the kit. No MIDI in: on the Move the panic arrives as MIDI, in Live as a
# reactivation (FilterDesigner.h).
ni_add_juce_plugin(filter-designer
    TARGET NIFilterDesigner
    NAME "NI Filter Designer"
    CODE FlDs
    ENGINE fd_engine
    CATEGORIES Fx EQ
    EDITOR
    SOURCES FilterDesigner.cpp FilterDesigner.h)

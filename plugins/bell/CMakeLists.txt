# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Bell: one NI Filter Designer filter, the bell, as a plugin of its
# own -- it boosts or cuts around Freq. Its engine is the single filters'
# (engines/single-filter, sf_engine), its processor and window the shell the
# nine share (plugins/_shared/single-filter); Bell.cpp names its shape.
ni_add_juce_plugin(bell
    TARGET NIBell
    NAME "NI Bell"
    CODE BeLl
    ENGINE sf_engine
    CATEGORIES Fx EQ
    SHARED_SHELL ni_single_filter
    SOURCES Bell.cpp)

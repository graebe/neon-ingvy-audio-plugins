# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Tilt: one NI Filter Designer filter, the tilt, as a plugin of its
# own -- it tips the whole spectrum about Freq. Its engine is the single filters'
# (engines/single-filter, sf_engine), its processor and window the shell the
# nine share (plugins/_shared/single-filter); Tilt.cpp names its shape.
ni_add_juce_plugin(tilt
    TARGET NITilt
    NAME "NI Tilt"
    CODE TiLt
    ENGINE sf_engine
    CATEGORIES Fx EQ
    SHARED_SHELL ni_single_filter
    SOURCES Tilt.cpp)

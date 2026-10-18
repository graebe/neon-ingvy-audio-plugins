# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI All Pass: one NI Filter Designer filter, the all pass, as a plugin of its
# own -- it turns the phase about Freq and leaves the level. Its engine is the single filters'
# (engines/single-filter, sf_engine), its processor and window the shell the
# nine share (plugins/_shared/single-filter); AllPass.cpp names its shape.
ni_add_juce_plugin(all-pass
    TARGET NIAllPass
    NAME "NI All Pass"
    CODE AlPs
    ENGINE sf_engine
    CATEGORIES Fx Filter
    SHARED_SHELL ni_single_filter
    SOURCES AllPass.cpp)

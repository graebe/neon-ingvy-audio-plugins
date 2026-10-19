# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI High Pass: one NI Filter Designer filter, the high pass, as a plugin of its
# own -- it takes away what is below Freq. Its engine is the single filters'
# (engines/single-filter, sf_engine), its processor and window the shell the
# nine share (plugins/_shared/single-filter); HighPass.cpp names its shape.
ni_add_juce_plugin(high-pass
    TARGET NIHighPass
    NAME "NI High Pass"
    CODE HiPs
    ENGINE sf_engine
    CATEGORIES Fx Filter
    SHARED_SHELL ni_single_filter
    SOURCES HighPass.cpp)

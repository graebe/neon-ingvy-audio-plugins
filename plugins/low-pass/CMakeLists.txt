# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Low Pass: one NI Filter Designer filter, the low pass, as a plugin of its
# own -- it takes away what is above Freq. Its engine is the single filters'
# (engines/single-filter, sf_engine), its processor and window the shell the
# nine share (plugins/_shared/single-filter); LowPass.cpp names its shape.
ni_add_juce_plugin(low-pass
    TARGET NILowPass
    NAME "NI Low Pass"
    CODE LoPs
    ENGINE sf_engine
    CATEGORIES Fx Filter
    SHARED_SHELL ni_single_filter
    SOURCES LowPass.cpp)

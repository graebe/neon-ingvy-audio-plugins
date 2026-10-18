# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Notch: one NI Filter Designer filter, the notch, as a plugin of its
# own -- it takes away a band around Freq. Its engine is the single filters'
# (engines/single-filter, sf_engine), its processor and window the shell the
# nine share (plugins/_shared/single-filter); Notch.cpp names its shape.
ni_add_juce_plugin(notch
    TARGET NINotch
    NAME "NI Notch"
    CODE NtCh
    ENGINE sf_engine
    CATEGORIES Fx Filter
    SHARED_SHELL ni_single_filter
    SOURCES Notch.cpp)

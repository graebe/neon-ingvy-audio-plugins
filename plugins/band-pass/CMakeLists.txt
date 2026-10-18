# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Band Pass: one NI Filter Designer filter, the band pass, as a plugin of its
# own -- it keeps a band around Freq. Its engine is the single filters'
# (engines/single-filter, sf_engine), its processor and window the shell the
# nine share (plugins/_shared/single-filter); BandPass.cpp names its shape.
ni_add_juce_plugin(band-pass
    TARGET NIBandPass
    NAME "NI Band Pass"
    CODE BdPs
    ENGINE sf_engine
    CATEGORIES Fx Filter
    SHARED_SHELL ni_single_filter
    SOURCES BandPass.cpp)

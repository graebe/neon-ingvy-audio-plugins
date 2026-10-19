# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Low Shelf: one NI Filter Designer filter, the low shelf, as a plugin of its
# own -- it boosts or cuts below Freq. Its engine is the single filters'
# (engines/single-filter, sf_engine), its processor and window the shell the
# nine share (plugins/_shared/single-filter); LowShelf.cpp names its shape.
ni_add_juce_plugin(low-shelf
    TARGET NILowShelf
    NAME "NI Low Shelf"
    CODE LoSh
    ENGINE sf_engine
    CATEGORIES Fx EQ
    SHARED_SHELL ni_single_filter
    SOURCES LowShelf.cpp)

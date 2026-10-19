# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI High Shelf: one NI Filter Designer filter, the high shelf, as a plugin of its
# own -- it boosts or cuts above Freq. Its engine is the single filters'
# (engines/single-filter, sf_engine), its processor and window the shell the
# nine share (plugins/_shared/single-filter); HighShelf.cpp names its shape.
ni_add_juce_plugin(high-shelf
    TARGET NIHighShelf
    NAME "NI High Shelf"
    CODE HiSh
    ENGINE sf_engine
    CATEGORIES Fx EQ
    SHARED_SHELL ni_single_filter
    SOURCES HighShelf.cpp)

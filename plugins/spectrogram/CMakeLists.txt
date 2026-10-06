# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Spectrogram. cmake/NiPlugin.cmake builds the editor and the bundles.
ni_add_plugin(NISpectrogram
  SOURCES
    Spectrogram.cpp Spectrogram.h
    State.cpp State.h
    Wire.cpp Wire.h
  LINK spectro_engine)

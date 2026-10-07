# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Spectrogram, on the JUCE shell (cmake/NiJucePlugin.cmake).
#
# NISpectrogram.vst3 is the iPlug2 bundle's name, so installing this build
# replaces that one rather than standing beside it with the same class; the
# class is the iPlug2 one (IPLUG2_CLASS), and its one parameter, the host's
# Bypass, keeps an index for an ID (LEGACY_PARAM_IDS) as every product's do.
# The analyzer is spectro_engine, which carries the Listen-In transport, the
# Ground's clock and the shell's handoff in its one archive. The editor is
# editor/, on the native kit.
ni_add_juce_plugin(spectrogram
    TARGET NISpectrogram
    NAME "NI Spectrogram"
    CODE SpGr
    CATEGORIES Fx Analyzer
    IPLUG2_CLASS
    LEGACY_PARAM_IDS
    EDITOR
    ENGINE spectro_engine
    SOURCES
        SpectrogramProcessor.cpp SpectrogramProcessor.h
        EngineModel.cpp EngineModel.h
        State.cpp State.h
        Wire.cpp Wire.h
        ${CMAKE_SOURCE_DIR}/plugins/_shared/ni/Wire.cpp)
target_include_directories(NISpectrogram PRIVATE ${CMAKE_SOURCE_DIR}/plugins/_shared)

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Trance Gate, on the JUCE shell (cmake/NiJucePlugin.cmake).
#
# NITranceGate.vst3 is the iPlug2 bundle's name, so installing this build
# replaces that one rather than standing beside it with the same class; the
# class is the iPlug2 one (IPLUG2_CLASS), and the parameters keep their
# iPlug2 indices as their VST3 IDs (LEGACY_PARAM_IDS). The editor is
# editor/, on the native kit. The shell's numbers and its scope are
# plugins/_shared/ni's, the plain C++ every processor shares.
ni_add_juce_plugin(trance-gate
    TARGET NITranceGate
    NAME "NI Trance Gate"
    CODE TrGt
    CATEGORIES Fx Modulation
    IPLUG2_CLASS
    LEGACY_PARAM_IDS
    EDITOR
    ENGINE tg_engine
    SOURCES
        TranceGate.cpp TranceGate.h
        Params.cpp Params.h
        EngineModel.cpp EngineModel.h
        Window.cpp Window.h
        ${CMAKE_SOURCE_DIR}/plugins/_shared/ni/Wire.cpp)
target_include_directories(NITranceGate PRIVATE ${CMAKE_SOURCE_DIR}/plugins/_shared)

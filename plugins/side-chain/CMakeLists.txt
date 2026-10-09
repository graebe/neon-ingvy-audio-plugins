# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Side-Chain, on the JUCE shell (cmake/NiJucePlugin.cmake).
#
# NISideChain.vst3 is the iPlug2 bundle's name, so installing this build
# replaces that one rather than standing beside it with the same class; the
# class is the iPlug2 one (IPLUG2_CLASS), and the parameters keep their
# iPlug2 indices as their VST3 IDs (LEGACY_PARAM_IDS). MIDI_IN is the
# trigger's input; the sidechain bus is declared by the processor
# (SideChain.cpp). The editor is editor/, on the native kit. The scope and
# the shells' numbers are plugins/_shared/ni's, the plain C++ every processor
# shares.
ni_add_juce_plugin(side-chain
    TARGET NISideChain
    NAME "NI Side-Chain"
    CODE SdCh
    CATEGORIES Fx Dynamics
    MIDI_IN
    IPLUG2_CLASS
    LEGACY_PARAM_IDS
    EDITOR
    ENGINE sc_engine
    SOURCES
        SideChain.cpp SideChain.h
        Params.cpp Params.h
        EngineModel.cpp EngineModel.h
        Kick.cpp Kick.h
        ${CMAKE_SOURCE_DIR}/plugins/_shared/ni/Wire.cpp)
target_include_directories(NISideChain PRIVATE ${CMAKE_SOURCE_DIR}/plugins/_shared)

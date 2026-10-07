# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Listen-In, on the JUCE shell (cmake/NiJucePlugin.cmake).
#
# NIListenIn.vst3 is the iPlug2 bundle's name, so installing this build
# replaces that one rather than standing beside it with the same class; the
# class is the iPlug2 one (IPLUG2_CLASS), and Bus keeps its iPlug2 index as
# its VST3 ID (LEGACY_PARAM_IDS). Fx|Tools, as the iPlug2 build filed it: a
# utility, not an analyzer. It has no DSP: the house transport, audio-bus, is
# its whole engine, and carries the Ground's and the shell handoff's C ABI.
# The editor is editor/, on the native kit.
ni_add_juce_plugin(listen-in
    TARGET NIListenIn
    NAME "NI Listen-In"
    CODE LsnI
    CATEGORIES Fx Tools
    IPLUG2_CLASS
    LEGACY_PARAM_IDS
    EDITOR
    ENGINE audio_bus_engine
    SOURCES
        ListenIn.cpp ListenIn.h
        EngineModel.cpp EngineModel.h
        State.cpp State.h
        Wire.cpp Wire.h)
target_include_directories(NIListenIn PRIVATE ${CMAKE_SOURCE_DIR}/plugins/_shared)

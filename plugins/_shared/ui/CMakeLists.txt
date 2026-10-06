# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# ni_ui -- Ultraviolet in JUCE: the kit every native editor is built from.
#
#   add_subdirectory(<JUCE>)               first: the kit links its modules
#   add_subdirectory(plugins/_shared/ui)   ni_ui, ni_ui_assets, ni_ui_add_editor()
#   ni_ui_add_editor(trance-gate)          ni_editor_trance_gate, and so on
#   target_link_libraries(<binary> PRIVATE ni_editor_trance_gate)
#
# The kit's sources are every src/*.cpp, found by a CONFIGURE_DEPENDS glob, so
# a new component is a new file and nothing here changes; the same holds for
# an editor's plugins/<product>/editor/*.cpp. docs/tech/native-ui.md is the
# architecture; ui/CMakeLists.txt builds the kit on its own, with its tests and
# its gallery.
#
# AN INTERFACE LIBRARY THAT CARRIES ITS SOURCES, NOT A STATIC LIBRARY, and that
# is JUCE's rule rather than a taste. A JUCE module is compiled into the binary
# that uses it, once, with that binary's configuration -- which is why JUCE's
# own module targets are interface libraries that hand their .cpp files to
# whoever links them. A static ni_ui would have to choose between two wrongs:
# compile the GUI modules itself, and the plugin's shared code (which must
# compile them too, through juce_audio_plugin_client) links a second copy of
# every JUCE symbol; or compile only the kit against JUCE's headers, and copy
# by hand the definitions JUCE gives each target (JUCE_MODULE_AVAILABLE_*,
# JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED, the debug flags), where the first JUCE
# update that changes one is an ODR violation nobody sees. As an interface
# library the kit's files are compiled once in each binary -- the test
# programs, the gallery, each plugin -- beside the JUCE they link, with the
# same flags. It is the arrangement JUCE's CMake documentation gives for
# modules, and the one the common JUCE project templates use for shared code.

if (NOT COMMAND juce_add_binary_data)
    message(FATAL_ERROR
        "plugins/_shared/ui needs JUCE: add_subdirectory(external/JUCE) before it.")
endif()

get_filename_component(NI_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)

# ------------------------------------------------------------------ assets
#
# What the kit draws with and does not draw itself: the two faces of JetBrains
# Mono (fonts/README.md has their provenance), Bravura for the notation
# (fonts/bravura/README.md), the design system's fifteen
# icons straight from its mirror, and the window ground's grain tile, which
# scripts/gen-tokens.mjs takes out of the design's stylesheet. Embedded, so a
# plugin finds them wherever its bundle is and a test needs no files at all.
file(GLOB NI_UI_ICONS CONFIGURE_DEPENDS
     ${NI_ROOT}/design/scheme/project/assets/Icons/*.svg)

juce_add_binary_data(ni_ui_assets
    HEADER_NAME NiUiAssets.h
    NAMESPACE ni_ui_assets
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts/JetBrainsMono-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts/JetBrainsMono-Medium.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts/bravura/Bravura.otf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/ground-grain.png
        ${NI_UI_ICONS})

# The OFL travels with the font: a plugin build copies this into its bundle.
set(NI_UI_FONT_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/fonts/OFL.txt)
set(NI_UI_FONT_LICENSE ${NI_UI_FONT_LICENSE} PARENT_SCOPE)
# Bravura's, likewise, for a plugin whose editor draws notation.
set(NI_UI_MUSIC_FONT_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/fonts/bravura/OFL.txt)
set(NI_UI_MUSIC_FONT_LICENSE ${NI_UI_MUSIC_FONT_LICENSE} PARENT_SCOPE)

# --------------------------------------------------------------------- kit

file(GLOB NI_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(ni_ui INTERFACE)
target_sources(ni_ui INTERFACE ${NI_UI_SOURCES})
target_include_directories(ni_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# No WebView and no libcurl, in anything that draws with the kit: the editors
# are native Components, and nothing in them fetches a URL. juce_gui_extra
# (which juce_audio_processors brings, for ParameterAttachment) would
# otherwise link WebKit on macOS and want webkit2gtk on Linux.
target_compile_definitions(ni_ui INTERFACE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

# The sources are UTF-8 -- an info line's dash is -- and a compiler with the
# Visual C++ command line reads them as the system's code page unless told.
if (MSVC)
    target_compile_options(ni_ui INTERFACE /utf-8)
endif()

target_link_libraries(ni_ui INTERFACE
    ni_ui_assets
    juce::juce_core
    juce::juce_events
    juce::juce_data_structures
    juce::juce_graphics
    juce::juce_gui_basics
    # juce::RangedAudioParameter and juce::ParameterAttachment, which is how a
    # control binds to a host parameter (ParamBinding.h).
    juce::juce_audio_processors)

# ----------------------------------------------------------------- editors
#
# ni_ui_add_editor(<product>) -- plugins/<product>/editor as one library,
# ni_editor_<product> with the dashes as underscores: every .cpp there, its
# headers by their bare names, and the kit. An editor with no .cpp yet is a
# library of headers, and becomes one with sources the build after its first
# .cpp appears.
function(ni_ui_add_editor product)
    string(REPLACE "-" "_" id ${product})
    set(dir ${NI_ROOT}/plugins/${product}/editor)
    file(GLOB sources CONFIGURE_DEPENDS ${dir}/*.cpp)
    add_library(ni_editor_${id} INTERFACE)
    target_sources(ni_editor_${id} INTERFACE ${sources})
    target_include_directories(ni_editor_${id} INTERFACE ${dir})
    target_link_libraries(ni_editor_${id} INTERFACE ni_ui)
endfunction()

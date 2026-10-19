# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# THE SINGLE FILTERS' SHELL: the processor and the editor the nine products
# share (NI High Pass ... NI All Pass, the root build's NI_SINGLE_FILTERS),
# each product's plugins/<product> being its build and one line that names
# its shape. Interface libraries that carry their sources, for the kit's
# reason (plugins/_shared/ui/CMakeLists.txt): compiled once in each binary
# that links them, beside the JUCE it links.
#
#   ni_editor_single_filter   the editor (editor/*.cpp) on the kit -- what
#                             the editor's tests link, with fakes for the
#                             model (tests/ui/single-filter_*)
#   ni_single_filter          the processor (SingleFilter.cpp) with the
#                             editor and the engine: what a product's
#                             ni_add_juce_plugin takes as its SHARED_SHELL

file(GLOB _sf_editor CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp)
add_library(ni_editor_single_filter INTERFACE)
target_sources(ni_editor_single_filter INTERFACE ${_sf_editor})
target_include_directories(ni_editor_single_filter INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/editor)
target_link_libraries(ni_editor_single_filter INTERFACE ni_ui)

add_library(ni_single_filter INTERFACE)
target_sources(ni_single_filter INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/SingleFilter.cpp)
target_include_directories(ni_single_filter INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ni_single_filter INTERFACE ni_editor_single_filter sf_engine)

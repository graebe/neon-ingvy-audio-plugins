# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The gallery: every kit component in each of its states, in one window, for a
# person to look at -- and, from the command line, as PNGs:
#
#   "NI UI Gallery" --shots <dir> [--scale 2]    every page, then quit
#   "NI UI Gallery" --page <name>                open on one page
#
# A page is a file in pages/, found by a CONFIGURE_DEPENDS glob and registered
# by itself (NI_GALLERY_PAGE in Gallery.h), so a new component adds its page
# without touching anything shared. The pages are also a library the test
# programs link: a page is a ready-made scene for a snapshot.
#
# Not shipped and not installed: nothing outside ui/CMakeLists.txt adds this
# directory.

file(GLOB NI_GALLERY_PAGES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/pages/*.cpp)

add_library(ni_ui_gallery_pages INTERFACE)
target_sources(ni_ui_gallery_pages INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Gallery.cpp
    ${NI_GALLERY_PAGES})
target_include_directories(ni_ui_gallery_pages INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ni_ui_gallery_pages INTERFACE ni_ui)

juce_add_gui_app(ni_ui_gallery
    PRODUCT_NAME "NI UI Gallery"
    COMPANY_NAME "Neon Ingvy"
    BUNDLE_ID com.neoningvy.ni-ui-gallery)
target_sources(ni_ui_gallery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp)
target_link_libraries(ni_ui_gallery PRIVATE
    ni_ui_gallery_pages
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

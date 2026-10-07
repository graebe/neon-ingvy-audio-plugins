# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# How a Neon Ingvy plugin is built: the iPlug2 bundle around its engine.
#
#   ni_add_plugin(<NAME> SOURCES <files> LINK <targets>)
#
# Include it once, at the root, after project(), iPlug2 and cmake/NiRust.cmake,
# which builds the engines a plugin LINKs.

# ------------------------------------------------------------ the plugins
#
# THE NOTICES TRAVEL INSIDE EVERY BUNDLE. The GPL asks that every copy come
# with the licence, the permissive licences of what is compiled in ask the same
# of their notices, and a bundle is copied on its own.
# scripts/check-licenses.mjs --bundles checks they arrived.
set(NI_BUNDLE_NOTICES
    ${CMAKE_SOURCE_DIR}/LICENSE
    ${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES.md)

# The shell every plugin is built on (plugins/_shared). Compiled into each
# format target -- iplug::Plugin is a different class under each API -- so it
# is sources and include paths, not a library.
set(NI_SHELL_DIR ${CMAKE_SOURCE_DIR}/plugins/_shared)
set(NI_SHELL_SOURCES
    ${NI_SHELL_DIR}/ni/WebPlugin.cpp
    ${NI_SHELL_DIR}/ni/Editor.cpp
    ${NI_SHELL_DIR}/ni/Wire.cpp)
add_library(ni_shell INTERFACE)
target_include_directories(ni_shell INTERFACE ${NI_SHELL_DIR})

# resources/web is vite's output and untracked; a plugin whose editor did not
# build must not configure (it would install a white window over a working
# one). See the file.
include(${CMAKE_SOURCE_DIR}/cmake/EditorGuard.cmake)
find_program(NI_NPM npm)

# ni_add_plugin(<NAME> SOURCES <files...> LINK <targets...>)
#
# From the calling directory: its ui/ built by vite into resources/web, and
# the bundle as VST3, CLAP and AU with a WebView editor. The target name is the
# bundle name, which config.h's BUNDLE_NAME must spell identically -- the AU
# finds its own bundle by it. No APP: the standalone's main() needs resource
# IDs that exist only for an IGraphics UI.
function(ni_add_plugin name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LINK" ${ARGN})
    set(dir ${CMAKE_CURRENT_SOURCE_DIR})
    get_filename_component(product ${dir} NAME)

    # VITE AT CONFIGURE TIME, so the glob below finds the editor, AND AT BUILD
    # TIME, so an edit to ui/src reaches the bundle. A stale editor looks
    # exactly like a broken one.
    if (NI_NPM)
        execute_process(
            COMMAND ${NI_NPM} --prefix ${dir}/ui run build
            RESULT_VARIABLE ui_result OUTPUT_QUIET ERROR_QUIET)
        if (NOT ui_result EQUAL 0)
            message(WARNING "The ${product} editor did not build; run `npm ci` at the repository root")
        endif()
        add_custom_target(${name}UI ALL
            COMMAND ${NI_NPM} --prefix ${dir}/ui run build
            WORKING_DIRECTORY ${dir}
            COMMENT "Building the ${product} editor (vite)"
            VERBATIM)
    else()
        message(WARNING "npm not found -- the editor cannot be built (resources/web is build output)")
    endif()

    # A file that is not globbed does not arrive in the bundle.
    file(GLOB web
        "${dir}/resources/web/*.html"
        "${dir}/resources/web/assets/*"
        "${dir}/resources/web/fonts/*")
    ni_require_editor(${product} ${web})

    iplug_add_plugin(${name}
        SOURCES ${ARG_SOURCES} ${NI_SHELL_SOURCES} config.h resources/resource.h
        FORMATS VST3 CLAP AU
        UI WEBVIEW
        WEB_RESOURCES ${web}
        RESOURCES ${NI_BUNDLE_NOTICES}
        LINK ni_shell ground_engine shell_engine ${ARG_LINK})

    if (NOT TARGET ${name}UI)
        return()
    endif()
    foreach(fmt vst3 clap au)
        if (NOT TARGET ${name}-${fmt})
            continue()
        endif()
        # Each format copies the web resources itself, so it waits for vite.
        add_dependencies(${name}-${fmt} ${name}UI)
        # AND ITS LINK DEPENDS ON THEM: iPlug2 deploys to ~/Library from a
        # POST_BUILD step, which runs only on a relink -- without this a UI-only
        # change rebuilt build/out and left the installed plugin as it was.
        set_property(TARGET ${name}-${fmt} APPEND PROPERTY LINK_DEPENDS "${web}")

        # AND THE DEPLOYED EDITOR IS PUT BACK LAST. CMake copies
        # MACOSX_PACKAGE_LOCATION resources after the POST_BUILD commands, so
        # iPlug2's deploy can publish the previous build's editor, or none on a
        # first build. POST_BUILD commands run in the order added and
        # iplug_add_plugin added its own first, so this copies from the source
        # tree -- what vite just wrote -- after it.
        if (fmt STREQUAL "vst3")
            set(deploy "${IPLUG_DEPLOY_PATH_VST3}")
            set(ext "vst3")
        elseif (fmt STREQUAL "clap")
            set(deploy "${IPLUG_DEPLOY_PATH_CLAP}")
            set(ext "clap")
        else()
            set(deploy "${IPLUG_DEPLOY_PATH_AU}")
            set(ext "component")
        endif()
        if (deploy)
            add_custom_command(TARGET ${name}-${fmt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${dir}/resources/web"
                        "${deploy}/${name}.${ext}/Contents/Resources/web"
                COMMAND ${CMAKE_COMMAND} -E copy ${NI_BUNDLE_NOTICES}
                        "${deploy}/${name}.${ext}/Contents/Resources/"
                COMMENT "Refreshing ${name}.${ext}'s editor in ${deploy}"
                VERBATIM)
        endif()
    endforeach()
endfunction()

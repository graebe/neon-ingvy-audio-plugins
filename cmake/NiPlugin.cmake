# How a Neon Ingvy plugin is built: its Rust engine, and the iPlug2 bundle
# around it.
#
#   ni_add_rust_engine(<target> CRATE <crate> LIB <lib> INCLUDE <dir>)
#   ni_add_rust_headers(<target> INCLUDE <dir>)
#   ni_build_rust_engines()           once, after every ni_add_rust_engine
#   ni_add_plugin(<NAME> SOURCES <files> LINK <targets>)
#
# Include it once, at the root, after project() and iPlug2.

include(${CMAKE_SOURCE_DIR}/cmake/RustToolchain.cmake)

# ------------------------------------------------------------ the engines
#
# ONE CARGO INVOCATION, ONE TARGET DIRECTORY. Every product's capi crate is
# built together, so the crates they share -- ni-dsp, ground, shell, audio-bus --
# compile once rather than once per product.
#
# ONE STATIC LIBRARY PER PLUGIN STILL. Each product's capi crate is a staticlib
# that absorbs the rlibs it depends on (engines/spectro/crates/spectro-capi's
# Cargo.toml states the rule): two Rust staticlibs in one binary each carry the
# Rust runtime and fail to link. So a plugin links exactly its own archive, and
# a crate with no product of its own -- ground, shell -- is headers only here.
#
# CARGO IS THE DEPENDENCY SCANNER: the target always runs, cargo is a no-op
# when nothing changed, and copy_if_different keeps an unchanged archive's
# timestamp so nothing relinks.

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/Cargo.toml)
    message(FATAL_ERROR "No Cargo.toml at the repository root -- the workspace is missing.")
endif()

set(NI_RUST_TARGET_DIR ${CMAKE_BINARY_DIR}/rust)
set(NI_RUST_LIB_DIR ${CMAKE_BINARY_DIR}/rust-lib)

# CMake's Apple architecture names are not Rust's target triples.
set(NI_RUST_TRIPLES "")
foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
    if (arch STREQUAL "arm64")
        list(APPEND NI_RUST_TRIPLES aarch64-apple-darwin)
    elseif (arch STREQUAL "x86_64")
        list(APPEND NI_RUST_TRIPLES x86_64-apple-darwin)
    else()
        message(FATAL_ERROR "no Rust target known for OSX architecture '${arch}'")
    endif()
endforeach()

function(ni_add_rust_engine name)
    cmake_parse_arguments(ARG "" "CRATE;LIB" "INCLUDE" ${ARGN})
    if (NOT ARG_CRATE OR NOT ARG_LIB OR NOT ARG_INCLUDE)
        message(FATAL_ERROR "ni_add_rust_engine(${name}) needs CRATE, LIB and INCLUDE")
    endif()
    set_property(GLOBAL APPEND PROPERTY NI_RUST_CRATES ${ARG_CRATE})
    set_property(GLOBAL APPEND PROPERTY NI_RUST_LIBS ${ARG_LIB})
    add_library(${name} INTERFACE)
    target_link_libraries(${name} INTERFACE ${NI_RUST_LIB_DIR}/lib${ARG_LIB}.a)
    target_include_directories(${name} INTERFACE ${ARG_INCLUDE})
    add_dependencies(${name} ni_rust_engines)
endfunction()

# A crate whose C ABI rides inside every product's archive: its header, and no
# archive of its own.
function(ni_add_rust_headers name)
    cmake_parse_arguments(ARG "" "" "INCLUDE" ${ARGN})
    add_library(${name} INTERFACE)
    target_include_directories(${name} INTERFACE ${ARG_INCLUDE})
endfunction()

function(ni_build_rust_engines)
    get_property(crates GLOBAL PROPERTY NI_RUST_CRATES)
    get_property(libs GLOBAL PROPERTY NI_RUST_LIBS)

    set(cargo_args build --release --target-dir ${NI_RUST_TARGET_DIR})
    foreach(crate IN LISTS crates)
        list(APPEND cargo_args -p ${crate})
    endforeach()
    foreach(triple IN LISTS NI_RUST_TRIPLES)
        list(APPEND cargo_args --target ${triple})
    endforeach()

    set(combine "")
    set(archives "")
    foreach(lib IN LISTS libs)
        set(out ${NI_RUST_LIB_DIR}/lib${lib}.a)
        set(slices "")
        foreach(triple IN LISTS NI_RUST_TRIPLES)
            list(APPEND slices ${NI_RUST_TARGET_DIR}/${triple}/release/lib${lib}.a)
        endforeach()
        if (NOT slices)
            # No OSX_ARCHITECTURES: one host build, no triple in the path.
            set(slices ${NI_RUST_TARGET_DIR}/release/lib${lib}.a)
        endif()
        list(LENGTH slices n)
        if (n GREATER 1)
            list(APPEND combine COMMAND lipo -create ${slices} -output ${out}.new)
        else()
            list(APPEND combine COMMAND ${CMAKE_COMMAND} -E copy ${slices} ${out}.new)
        endif()
        list(APPEND combine COMMAND ${CMAKE_COMMAND} -E copy_if_different ${out}.new ${out})
        list(APPEND archives ${out})
    endforeach()

    add_custom_target(ni_rust_engines ALL
        BYPRODUCTS ${archives}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${NI_RUST_LIB_DIR}
        COMMAND ${RUST_ENV} ${RUST_CARGO} ${cargo_args}
        ${combine}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Building the Rust engines (cargo: ${crates}; ${NI_RUST_TRIPLES})"
        VERBATIM)
endfunction()

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
# The system's save and open panels (ni/FileDialog.h) and its clipboard
# (ni/Clipboard.h): AppKit glue, under ARC like iPlug2's own WebView sources,
# and UTType for the panels' file types.
if (APPLE)
    list(APPEND NI_SHELL_SOURCES ${NI_SHELL_DIR}/ni/FileDialog.mm ${NI_SHELL_DIR}/ni/Clipboard.mm)
    target_link_libraries(ni_shell INTERFACE "-framework UniformTypeIdentifiers")
endif()

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

    # A source property is the calling directory's, so it is set here, where
    # the plugin's targets are made.
    if (APPLE)
        set_source_files_properties(${NI_SHELL_DIR}/ni/FileDialog.mm ${NI_SHELL_DIR}/ni/Clipboard.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    endif()
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

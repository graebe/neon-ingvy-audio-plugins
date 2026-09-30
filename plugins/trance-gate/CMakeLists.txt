# NI Trance Gate, on iPlug2.
#
# THE TARGET NAME IS THE BUNDLE NAME. iPlug2 builds the bundle identifier from
# DOMAIN.MFR.type.<target> and the AU looks its own bundle up by that identifier
# to find the Cocoa view -- so BUNDLE_NAME in config.h must be spelled exactly as
# it is here, or CFBundleCopyBundleURL returns NULL and the host segfaults the
# moment anything asks for the editor. It also names the four Info.plists, which
# iPlug2 finds as resources/<target>-<FORMAT>-Info.plist.
#
# iPlug2 is zlib-licensed and the VST3 SDK it fetches is MIT -- Steinberg
# withdrew the GPLv3 and proprietary options -- so this target and everything
# it links are permissive. That is the whole reason it exists: JUCE is
# GPLv3-or-commercial and was the only thing keeping this repository from
# being MIT.

set(IPLUG2_DIR ${CMAKE_SOURCE_DIR}/external/iPlug2 CACHE PATH "iPlug2 root")
include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

# The editor is a Solid + Vite app in ui/, built into resources/web. Run
# `npm --prefix ui run build` after changing it; the glob below is what iPlug2
# copies into the bundle, and a file that is not globbed simply does not
# arrive -- a blank editor with no error anywhere.
# BUILD THE UI BEFORE GLOBBING IT.
#
# execute_process runs at CONFIGURE time, which is not the same as build time
# -- `cmake --build` then never re-ran vite, and the plugin shipped whatever
# resources/web last contained. An edit to ui/src reached the bundle only if
# somebody also re-ran cmake, and a stale editor looks exactly like a broken
# one. That cost an hour: the scale fix was in the source and not in the
# bundle, and the rendered page disproved a change that had never been built.
#
# So: configure-time once (so the glob below has something to find), and a
# build-time target that every format depends on.
find_program(TG_NPM npm)
if (TG_NPM)
  execute_process(
    COMMAND ${TG_NPM} --prefix ${CMAKE_CURRENT_SOURCE_DIR}/ui run build
    RESULT_VARIABLE TG_UI_RESULT OUTPUT_QUIET ERROR_QUIET)
  if (NOT TG_UI_RESULT EQUAL 0)
    message(WARNING "The editor did not build; run `npm ci` at the repository root")
  endif()

  add_custom_target(NITranceGateUI ALL
    COMMAND ${TG_NPM} --prefix ${CMAKE_CURRENT_SOURCE_DIR}/ui run build
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building the editor (vite)"
    VERBATIM)
else()
  message(WARNING "npm not found -- the editor cannot be built (resources/web is build output)")
endif()

file(GLOB TG_WEB
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/*.html"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/assets/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/fonts/*")

# A plugin with no editor must not configure -- see cmake/EditorGuard.cmake.
ni_require_editor(trance-gate ${TG_WEB})

iplug_add_plugin(NITranceGate
  SOURCES
    TranceGate.cpp
    TranceGate.h
    Params.cpp
    Params.h
    Patch.cpp
    Patch.h
    Wire.cpp
    Wire.h
    config.h
    resources/resource.h
  # No APP: the standalone's main() and its preferences dialog reference menu
  # and combo-box resource IDs that only exist for a plugin with a UI, so it
  # cannot build until there is one. VST3 is what Live loads, AU what Logic
  # loads, and CLAP costs nothing now the SDK is in place.
  FORMATS
    VST3 CLAP AU
  UI WEBVIEW
  WEB_RESOURCES ${TG_WEB}
  # LICENSE and THIRD_PARTY_LICENSES.md, into Contents/Resources -- see the
  # root CMakeLists.
  RESOURCES ${NI_BUNDLE_NOTICES}
  # ground_engine is a HEADER ONLY -- the detector's C ABI is compiled into
  # libtg_capi.a via tg-capi's dependency on ground-capi, because one archive per
  # plugin is an invariant here. cmake/GroundEngine.cmake says why.
  LINK
    tg_engine
    ground_engine
    shell_engine)

# The bundle's web resources are copied by each format target, so each of them
# must wait for vite rather than race it.
if (TARGET NITranceGateUI)
  foreach(fmt vst3 clap au)
    if (TARGET NITranceGate-${fmt})
      add_dependencies(NITranceGate-${fmt} NITranceGateUI)
      # AND THE LINK MUST DEPEND ON THE BUNDLE'S WEB FILES, or a UI-only change
      # never reaches the installed plugin.
      #
      # add_dependencies above is ORDERING ONLY: it guarantees vite has run, not
      # that anything is copied anywhere. iPlug2 deploys to ~/Library with a
      # POST_BUILD command, and POST_BUILD runs only when the target actually
      # relinks -- so editing ui/src rebuilt the bundle inside build/out and left
      # the INSTALLED plugin untouched. The next test in Live then loaded
      # yesterday's editor, with nothing anywhere saying so.
      #
      # LINK_DEPENDS puts the web files on the link rule, so a changed bundle
      # relinks the plugin, and the relink is what fires the deploy.
      set_property(TARGET NITranceGate-${fmt} APPEND PROPERTY LINK_DEPENDS "${TG_WEB}")

      # ... AND THEN THE WEB FILES HAVE TO BE PUT BACK, because iPlug2's deploy
      # races the thing it is deploying.
      #
      # CMake copies MACOSX_PACKAGE_LOCATION resources -- which is how every file
      # under resources/web gets into the bundle -- AFTER the target's POST_BUILD
      # commands, not before. The build log says so plainly once you look for it:
      #
      #   [iPlug2] Copying plugin: ~/Library/Audio/Plug-Ins/CLAP/NIListenIn.clap
      #   Copying OS X content out/NIListenIn.clap/Contents/Resources/web/assets/style.css
      #
      # So iPlug2's `rm -rf` + `copy_directory` can publish a bundle whose editor
      # is the PREVIOUS build's, or -- on a first build -- missing entirely, which
      # is a WebView that comes up blank with no error.
      #
      # This runs last (POST_BUILD commands run in the order they are added, and
      # iplug_add_plugin added its own before this file got here) and copies the
      # editor straight from the source tree, which is what vite just wrote. It
      # does not depend on the bundle's own copy having happened at all.
      if (fmt STREQUAL "vst3")
        set(_ni_deploy "${IPLUG_DEPLOY_PATH_VST3}")
        set(_ni_ext "vst3")
      elseif (fmt STREQUAL "clap")
        set(_ni_deploy "${IPLUG_DEPLOY_PATH_CLAP}")
        set(_ni_ext "clap")
      else()
        set(_ni_deploy "${IPLUG_DEPLOY_PATH_AU}")
        set(_ni_ext "component")
      endif()
      if (_ni_deploy)
        add_custom_command(TARGET NITranceGate-${fmt} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_directory
                  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web"
                  "${_ni_deploy}/NITranceGate.${_ni_ext}/Contents/Resources/web"
          COMMAND ${CMAKE_COMMAND} -E copy ${NI_BUNDLE_NOTICES}
                  "${_ni_deploy}/NITranceGate.${_ni_ext}/Contents/Resources/"
          COMMENT "Refreshing NITranceGate.${_ni_ext}'s editor in ${_ni_deploy}"
          VERBATIM)
      endif()
    endif()
  endforeach()
endif()

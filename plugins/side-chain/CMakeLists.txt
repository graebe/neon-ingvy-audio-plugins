# NI Side-Chain, on iPlug2.
#
# The one thing here that neither sibling plugin does: an AUX INPUT BUS. See
# config.h's PLUG_CHANNEL_IO. It needs nothing special from cmake -- iPlug2 reads
# the bus layout from that macro -- but it is the reason this target's AU is an
# `aumf` rather than an `aufx`, which the Info.plist states literally.

set(IPLUG2_DIR ${CMAKE_SOURCE_DIR}/external/iPlug2 CACHE PATH "iPlug2 root")
include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

# BUILD THE UI BEFORE GLOBBING IT, and then again at build time.
#
# execute_process runs at CONFIGURE time, which is not build time: with only
# that, `cmake --build` never re-runs vite and the plugin ships whatever
# resources/web last contained. An edit to ui/src then reaches the bundle only if
# somebody also re-runs cmake, and a stale editor looks exactly like a broken
# one. The Trance Gate lost an hour to it -- a fix that was in the source and not
# in the bundle, with the rendered page disproving a change that had never been
# built.
#
# So: configure-time once, so the glob below has something to find, plus a
# build-time target every format depends on.
find_program(SC_NPM npm)
if (SC_NPM)
  execute_process(
    COMMAND ${SC_NPM} --prefix ${CMAKE_CURRENT_SOURCE_DIR}/ui run build
    RESULT_VARIABLE SC_UI_RESULT OUTPUT_QUIET ERROR_QUIET)
  if (NOT SC_UI_RESULT EQUAL 0)
    message(WARNING "The editor did not build; run `npm ci` at the repository root")
  endif()

  add_custom_target(NISideChainUI ALL
    COMMAND ${SC_NPM} --prefix ${CMAKE_CURRENT_SOURCE_DIR}/ui run build
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building the NI Side-Chain editor (vite)"
    VERBATIM)
else()
  message(WARNING "npm not found -- the editor cannot be built (resources/web is build output)")
endif()

# A file that is not globbed simply does not arrive in the bundle: a blank
# editor, with no error anywhere.
file(GLOB SC_WEB
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/*.html"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/assets/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/fonts/*")

# A plugin with no editor must not configure -- see cmake/EditorGuard.cmake.
ni_require_editor(side-chain ${SC_WEB})

iplug_add_plugin(NISideChain
  SOURCES
    SideChain.cpp
    SideChain.h
    Wire.cpp
    Wire.h
    config.h
    resources/resource.h
  # No APP: the standalone's main() and its preferences dialog reference menu and
  # combo-box resource IDs that only exist for a plugin with a UI, so it cannot
  # build until there is one. VST3 is what Live loads, AU what Logic loads, and
  # CLAP costs nothing now the SDK is in place.
  FORMATS
    VST3 CLAP AU
  UI WEBVIEW
  WEB_RESOURCES ${SC_WEB}
  # LICENSE and THIRD_PARTY_LICENSES.md, into Contents/Resources -- see the
  # root CMakeLists.
  RESOURCES ${NI_BUNDLE_NOTICES}
  # ground_engine is a HEADER ONLY -- see cmake/GroundEngine.cmake. The detector
  # itself rides inside libsc_capi.a.
  LINK
    sc_engine
    ground_engine
    shell_engine)

# Each format target copies the web resources itself, so each must wait for vite
# rather than race it.
if (TARGET NISideChainUI)
  foreach(fmt vst3 clap au)
    if (TARGET NISideChain-${fmt})
      add_dependencies(NISideChain-${fmt} NISideChainUI)
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
      set_property(TARGET NISideChain-${fmt} APPEND PROPERTY LINK_DEPENDS "${SC_WEB}")

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
        add_custom_command(TARGET NISideChain-${fmt} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_directory
                  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web"
                  "${_ni_deploy}/NISideChain.${_ni_ext}/Contents/Resources/web"
          COMMAND ${CMAKE_COMMAND} -E copy ${NI_BUNDLE_NOTICES}
                  "${_ni_deploy}/NISideChain.${_ni_ext}/Contents/Resources/"
          COMMENT "Refreshing NISideChain.${_ni_ext}'s editor in ${_ni_deploy}"
          VERBATIM)
      endif()
    endif()
  endforeach()
endif()

# The Spectrogram, on iPlug2.
#
# Structurally the Trance Gate's target, and deliberately so: everything awkward
# here was learned once already next door. The two traps that cost real time are
# both about the editor being BUILT SOFTWARE rather than a file:
#
#   1. vite has to run at CONFIGURE time (so the glob below finds something)
#      AND at BUILD time (so an edit to ui/src actually reaches the bundle).
#      With only the first, `cmake --build` shipped whatever resources/web last
#      contained, and a stale editor looks exactly like a broken one.
#   2. A file that is not globbed simply does not arrive in the bundle. The
#      symptom is a blank editor with no error anywhere.

set(IPLUG2_DIR ${CMAKE_SOURCE_DIR}/external/iPlug2 CACHE PATH "iPlug2 root")
include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

find_program(SPECTRO_NPM npm)
if (SPECTRO_NPM)
  execute_process(
    COMMAND ${SPECTRO_NPM} --prefix ${CMAKE_CURRENT_SOURCE_DIR}/ui run build
    RESULT_VARIABLE SPECTRO_UI_RESULT OUTPUT_QUIET ERROR_QUIET)
  if (NOT SPECTRO_UI_RESULT EQUAL 0)
    message(WARNING "The editor did not build; run `npm ci` at the repository root")
  endif()

  add_custom_target(NISpectrogramUI ALL
    COMMAND ${SPECTRO_NPM} --prefix ${CMAKE_CURRENT_SOURCE_DIR}/ui run build
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building the Spectrogram editor (vite)"
    VERBATIM)
else()
  message(WARNING "npm not found -- the editor cannot be built (resources/web is build output)")
endif()

file(GLOB SPECTRO_WEB
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/*.html"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/assets/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/fonts/*")

# A plugin with no editor must not configure -- see cmake/EditorGuard.cmake.
ni_require_editor(spectrogram ${SPECTRO_WEB})

iplug_add_plugin(NISpectrogram
  SOURCES
    Spectrogram.cpp
    Spectrogram.h
    Wire.cpp
    Wire.h
    config.h
    resources/resource.h
  # No APP, for the Trance Gate's reason: the standalone's main() and its
  # preferences dialog reference resource IDs that only exist for an IGraphics
  # UI. VST3 is what Live loads, AU what Logic loads, CLAP costs nothing.
  FORMATS
    VST3 CLAP AU
  UI WEBVIEW
  WEB_RESOURCES ${SPECTRO_WEB}
  # LICENSE and THIRD_PARTY_LICENSES.md, into Contents/Resources -- see the
  # root CMakeLists.
  RESOURCES ${NI_BUNDLE_NOTICES}
  # ground_engine is a HEADER ONLY -- see cmake/GroundEngine.cmake. The detector
  # itself rides inside libspectro_capi.a.
  LINK
    spectro_engine
    ground_engine)

# The bundle's web resources are copied by each format target, so each of them
# must wait for vite rather than race it.
if (TARGET NISpectrogramUI)
  foreach(fmt vst3 clap au)
    if (TARGET NISpectrogram-${fmt})
      add_dependencies(NISpectrogram-${fmt} NISpectrogramUI)
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
      set_property(TARGET NISpectrogram-${fmt} APPEND PROPERTY LINK_DEPENDS "${SPECTRO_WEB}")

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
        add_custom_command(TARGET NISpectrogram-${fmt} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_directory
                  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web"
                  "${_ni_deploy}/NISpectrogram.${_ni_ext}/Contents/Resources/web"
          COMMAND ${CMAKE_COMMAND} -E copy ${NI_BUNDLE_NOTICES}
                  "${_ni_deploy}/NISpectrogram.${_ni_ext}/Contents/Resources/"
          COMMENT "Refreshing NISpectrogram.${_ni_ext}'s editor in ${_ni_deploy}"
          VERBATIM)
      endif()
    endif()
  endforeach()
endif()

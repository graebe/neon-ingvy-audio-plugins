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
    message(WARNING "The editor did not build; run: "
                    "npm --prefix plugins/spectrogram-iplug/ui install")
  endif()

  add_custom_target(SpectrogramUI ALL
    COMMAND ${SPECTRO_NPM} --prefix ${CMAKE_CURRENT_SOURCE_DIR}/ui run build
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building the Spectrogram editor (vite)"
    VERBATIM)
else()
  message(WARNING "npm not found -- using whatever is already in resources/web")
endif()

file(GLOB SPECTRO_WEB
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/*.html"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/assets/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/fonts/*")

iplug_add_plugin(SpectrogramIP
  SOURCES
    Spectrogram.cpp
    Spectrogram.h
    config.h
    resources/resource.h
  # No APP, for the Trance Gate's reason: the standalone's main() and its
  # preferences dialog reference resource IDs that only exist for an IGraphics
  # UI. VST3 is what Live loads, AU what Logic loads, CLAP costs nothing.
  FORMATS
    VST3 CLAP AU
  UI WEBVIEW
  WEB_RESOURCES ${SPECTRO_WEB}
  LINK
    spectro_engine)

# The bundle's web resources are copied by each format target, so each of them
# must wait for vite rather than race it.
if (TARGET SpectrogramUI)
  foreach(fmt vst3 clap au)
    if (TARGET SpectrogramIP-${fmt})
      add_dependencies(SpectrogramIP-${fmt} SpectrogramUI)
    endif()
  endforeach()
endif()

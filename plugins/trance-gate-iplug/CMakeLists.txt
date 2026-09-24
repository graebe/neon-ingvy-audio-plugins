# The Trance Gate, on iPlug2.
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
    message(WARNING "The editor did not build; run: "
                    "npm --prefix plugins/trance-gate-iplug/ui install")
  endif()

  add_custom_target(TranceGateUI ALL
    COMMAND ${TG_NPM} --prefix ${CMAKE_CURRENT_SOURCE_DIR}/ui run build
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building the editor (vite)"
    VERBATIM)
else()
  message(WARNING "npm not found -- using whatever is already in resources/web")
endif()

file(GLOB TG_WEB
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/*.html"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/assets/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/fonts/*")

iplug_add_plugin(TranceGateIP
  SOURCES
    TranceGate.cpp
    TranceGate.h
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
  LINK
    tg_engine)

# The bundle's web resources are copied by each format target, so each of them
# must wait for vite rather than race it.
if (TARGET TranceGateUI)
  foreach(fmt vst3 clap au)
    if (TARGET TranceGateIP-${fmt})
      add_dependencies(TranceGateIP-${fmt} TranceGateUI)
    endif()
  endforeach()
endif()

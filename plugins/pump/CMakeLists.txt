# NI Pump, on iPlug2.
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
find_program(PUMP_NPM npm)
if (PUMP_NPM)
  execute_process(
    COMMAND ${PUMP_NPM} --prefix ${CMAKE_CURRENT_SOURCE_DIR}/ui run build
    RESULT_VARIABLE PUMP_UI_RESULT OUTPUT_QUIET ERROR_QUIET)
  if (NOT PUMP_UI_RESULT EQUAL 0)
    message(WARNING "The editor did not build; run: npm install")
  endif()

  add_custom_target(NIPumpUI ALL
    COMMAND ${PUMP_NPM} --prefix ${CMAKE_CURRENT_SOURCE_DIR}/ui run build
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building the NI Pump editor (vite)"
    VERBATIM)
else()
  message(WARNING "npm not found -- using whatever is already in resources/web")
endif()

# A file that is not globbed simply does not arrive in the bundle: a blank
# editor, with no error anywhere.
file(GLOB PUMP_WEB
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/*.html"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/assets/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/fonts/*")

iplug_add_plugin(NIPump
  SOURCES
    Pump.cpp
    Pump.h
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
  WEB_RESOURCES ${PUMP_WEB}
  LINK
    pump_engine)

# Each format target copies the web resources itself, so each must wait for vite
# rather than race it.
if (TARGET NIPumpUI)
  foreach(fmt vst3 clap au)
    if (TARGET NIPump-${fmt})
      add_dependencies(NIPump-${fmt} NIPumpUI)
    endif()
  endforeach()
endif()

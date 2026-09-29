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
    message(WARNING "The editor did not build; run: "
                    "npm --prefix plugins/trance-gate/ui install")
  endif()

  add_custom_target(NITranceGateUI ALL
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

#
# AN EMPTY GLOB IS A PLUGIN WITH NO EDITOR, AND IT USED TO SHIP.
#
# The vite step above only WARNS when it fails -- a worktree where nobody ran
# `npm ci`, a broken lockfile, npm missing entirely. The glob then finds nothing,
# every format builds and links happily, and iPlug2's deploy step does
# `rm -rf` on the installed bundle before copying the new one over it. So a
# failed editor build does not merely produce a bad plugin: it REPLACES a working
# installed plugin with one whose Contents/Resources/web does not exist.
#
# What the user sees is a white window. Nothing in the build output says why --
# the warning scrolled past a thousand lines earlier, if it was even this build
# that produced it.
#
# So: refuse to configure. A build that stops with a message naming the fix is
# always better than one that succeeds and uninstalls the editor.
#
if (NOT TG_WEB)
  message(FATAL_ERROR
    "plugins/trance-gate/resources/web is empty, so this build would install a "
    "plugin with NO EDITOR over whatever is there now -- a white window with "
    "nothing in the log to explain it.\n"
    "Run:  npm ci  (at the repository root), then configure again.")
endif()

iplug_add_plugin(NITranceGate
  SOURCES
    TranceGate.cpp
    TranceGate.h
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
  LINK
    tg_engine)

# The bundle's web resources are copied by each format target, so each of them
# must wait for vite rather than race it.
if (TARGET NITranceGateUI)
  foreach(fmt vst3 clap au)
    if (TARGET NITranceGate-${fmt})
      add_dependencies(NITranceGate-${fmt} NITranceGateUI)
    endif()
  endforeach()
endif()

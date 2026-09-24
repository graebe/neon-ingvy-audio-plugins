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

# UI NONE, for now, and it is a placeholder rather than a decision. The old
# editor is ~2,600 lines of juce::Graphics; its replacement is either iPlug2's
# own IGraphics or a web UI in a WebView (the macro takes UI WEBVIEW and
# WEB_RESOURCES for exactly that). Landing the processor first means every
# format can be built and validated before a pixel is drawn.
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
  UI NONE
  # UI NONE drops the graphics LIBRARY but not the HEADER selection:
  # IPlugDelegate_select.h still reaches for IGraphicsEditorDelegate.h unless
  # NO_IGRAPHICS is defined, and the error names neither the macro nor the UI
  # argument.
  DEFINES
    NO_IGRAPHICS
  LINK
    tg_engine)

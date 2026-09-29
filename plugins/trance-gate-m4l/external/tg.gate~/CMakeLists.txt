# tg.gate~ -- the Trance Gate engine as a Max external.
#
# THE ENGINE IS THE SAME ENGINE. This links `tg_engine`, the INTERFACE target
# cmake/TranceGateEngine.cmake builds -- the same libtg_capi.a the VST3, the
# AU, the CLAP and both render tests link. That file explains at length why
# there is exactly one copy and not a per-consumer rebuild: "It sounds
# different in Live" is the hardest kind of bug to chase, and a second copy of
# the engine is how you get one. A Max external is the fifth consumer and
# changes none of that.
#
# max-sdk-base IS MIT (Cycling '74, 2021), which is the only reason this
# target could be added at all -- see the licence policy in AGENTS.md. It is a
# submodule at external/max-sdk-base, and its own CMake does the bundle
# wrapping, the Info.plist and the .mxo layout.

set(MAX_SDK_BASE ${CMAKE_SOURCE_DIR}/external/max-sdk-base
    CACHE PATH "max-sdk-base root")

if (NOT EXISTS ${MAX_SDK_BASE}/script/max-pretarget.cmake)
    message(FATAL_ERROR
        "max-sdk-base is missing -- run: git submodule update --init "
        "external/max-sdk-base")
endif()

# WHERE THE .mxo LANDS. The SDK's default is three levels up from here, which
# in this tree is plugins/externals -- beside the plugins rather than inside
# the device that owns it. This keeps it with its device, which is also what a
# Max search path has to be pointed at while authoring the patcher.
set(C74_LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_SOURCE_DIR}/plugins/trance-gate-m4l/externals")

# THE DIRECTORY NAME IS THE OBJECT NAME, and this include is what makes that
# true: max-pretarget.cmake calls project() with the folder it is included
# from, mapping "~" to "_tilde" for CMake's sake and back again for the
# bundle's. There is no variable to override it with -- the folder IS the
# declaration -- which is why this file sits in one called `tg.gate~` rather
# than beside its siblings.
include(${MAX_SDK_BASE}/script/max-pretarget.cmake)

include_directories(
    "${MAX_SDK_INCLUDES}"
    "${MAX_SDK_MSP_INCLUDES}")

add_library(${PROJECT_NAME} MODULE "tg.gate~.c" "wire.c")

# The engine's header lives behind tg_engine's INTERFACE include directory, so
# linking it is also how trance_gate_core.h is found. Nothing here names the
# path.
target_link_libraries(${PROJECT_NAME} PRIVATE tg_engine)

# A UNIVERSAL EXTERNAL, because Max 9 runs native on both and a Live Set does
# not know which machine will open it next. The engine is already lipo'd for
# the same two slices by TranceGateEngine.cmake, so this only has to ask for
# the same pair rather than arrange it.
if (APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        OSX_ARCHITECTURES "arm64;x86_64")
endif()

include(${MAX_SDK_BASE}/script/max-posttarget.cmake)

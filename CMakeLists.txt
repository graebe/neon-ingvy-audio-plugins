# vst-library -- plugin builds around engines that live elsewhere.
#
# The engines are submodules, not copies. The Trance Gate's DSP is the same
# Rust crate Schwung builds into the Move module, pinned by commit: a second
# copy would drift, and the symptom would be "it sounds different in Live",
# which is the hardest kind of bug to chase.
#
# EVERYTHING HERE IS PERMISSIVE. iPlug2 is zlib, the VST3 SDK is MIT, CLAP is
# MIT and the engine is MIT. JUCE was the one exception -- AGPLv3-or-commercial
# as of JUCE 8 -- and it is gone.
cmake_minimum_required(VERSION 3.22)
# OBJC/OBJCXX on Apple because iPlug2's macOS layer is Objective-C++ -- the
# AUv2 entry, the WKWebView editor and the standalone's Cocoa shell are all
# .mm. Without them CMake fails at generate time with a bare
# "CMAKE_OBJCXX_COMPILE_OBJECT is not set", which names neither iPlug2 nor
# Objective-C++.
if (APPLE)
    project(vst_library VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)
else()
    project(vst_library VERSION 0.1.0 LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if (APPLE)
    # Ableton Live is still commonly run as x86_64 under Rosetta; a universal
    # binary loads either way and costs only build time.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# One engine build, shared by the plugin and all four native test targets.
include(cmake/TranceGateEngine.cmake)
# The Spectrogram's analyzer. In-repo rather than a submodule because it has no
# second consumer yet -- see the comment at the top of the file.
include(cmake/SpectroEngine.cmake)

add_subdirectory(plugins/trance-gate)
add_subdirectory(plugins/spectrogram)

enable_testing()
add_subdirectory(tests)

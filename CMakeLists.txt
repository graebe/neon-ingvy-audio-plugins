# vst-library -- plugin builds around engines that live elsewhere.
#
# The engines are submodules, not copies. The Trance Gate's DSP is the same
# C file Schwung compiles into the Move module, pinned by commit: a second
# copy would drift, and the symptom would be "it sounds different in Live",
# which is the hardest kind of bug to chase.
cmake_minimum_required(VERSION 3.22)
project(vst_library VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if (APPLE)
    # Ableton Live is still commonly run as x86_64 under Rosetta; a universal
    # binary loads either way and costs only build time.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

add_subdirectory(plugins/trance-gate)

enable_testing()
add_subdirectory(tests)

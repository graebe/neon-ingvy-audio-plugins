# Sanitizers for the C and C++ tests.
#
#   cmake -B build-tsan -DNI_SANITIZE=thread  -DCMAKE_OSX_ARCHITECTURES=arm64
#   cmake -B build-asan -DNI_SANITIZE=address -DCMAKE_OSX_ARCHITECTURES=arm64
#
# SCOPED TO THE TESTS, not the plugins. tests/CMakeLists.txt calls
# ni_sanitize_here() once, at its top, so every target declared in tests/ and
# tests/cpp/ is instrumented and nothing that ships is: a sanitized plugin is
# not a configuration anybody should be able to install by accident.
#
# WHAT IT DOES NOT SEE. The engines are Rust static libraries built by cargo,
# which this flag does not reach, so a race wholly inside an engine is invisible
# here; the Rust side needs its own (nightly) -Zsanitizer run. What this does
# see is every C and C++ caller of those engines -- the ABI tests, the fork-based
# IPC test, the shells' Wire code -- and that is where a caller's threading
# mistakes live.
#
# ONE SLICE. The sanitizer runtimes are per-architecture and a universal link
# asks for both; pass -DCMAKE_OSX_ARCHITECTURES=<one arch>, which the top-level
# CMakeLists honours. Refused rather than guessed, so the error names the fix.

set(NI_SANITIZE "" CACHE STRING
    "Instrument the C/C++ tests with a sanitizer: thread, address or undefined")
set_property(CACHE NI_SANITIZE PROPERTY STRINGS "" thread address undefined)

if (NI_SANITIZE AND NOT NI_SANITIZE MATCHES "^(thread|address|undefined)$")
    message(FATAL_ERROR "NI_SANITIZE='${NI_SANITIZE}': use thread, address or undefined")
endif()
if (NI_SANITIZE AND APPLE)
    list(LENGTH CMAKE_OSX_ARCHITECTURES _ni_san_archs)
    if (_ni_san_archs GREATER 1)
        message(FATAL_ERROR
            "NI_SANITIZE needs one architecture; configure with "
            "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
endif()

# Adds the sanitizer to every target declared from here on in the calling
# directory and its subdirectories.
macro(ni_sanitize_here)
    if (NI_SANITIZE)
        add_compile_options(-fsanitize=${NI_SANITIZE} -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${NI_SANITIZE})
        message(STATUS "sanitize: ${NI_SANITIZE} on the C/C++ tests in ${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endmacro()

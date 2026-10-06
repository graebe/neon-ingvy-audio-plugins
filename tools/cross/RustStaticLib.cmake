# A Rust static library for whichever platform this CMake build targets.
#
#   ni_add_rust_staticlib(<target> MANIFEST <Cargo.toml> LIB <name> [INCLUDE <dir>])
#   ni_add_rust_test(<test> MANIFEST <Cargo.toml>)
#
# <target> is an INTERFACE library: linking it brings the archive, the system
# libraries the Rust standard library needs there, and the crate's C headers.
# <test> is a ctest that runs the crate's `cargo test` for the same target --
# under the image's runner (Wine) when that target is Windows.
#
# THE RUST TARGET FOLLOWS THE C++ BUILD, never the machine cargo runs on:
#
#   macOS    a slice per CMAKE_OSX_ARCHITECTURES entry, joined with lipo: the
#            plugin is universal, so the archive it links must be too
#   Linux    the host's triple -- Linux is built natively, in its container
#   Windows  <arch>-pc-windows-msvc, built for the C runtime that
#            CMAKE_MSVC_RUNTIME_LIBRARY gives the C++ (see below)
#
# cargo's target directory is this BUILD directory's own: a macOS, a Linux and
# a Windows build of one checkout -- two of them written from inside
# containers -- never share Rust objects.
#
# CARGO IS THE DEPENDENCY SCANNER, as in cmake/NiPlugin.cmake: the target
# always runs, cargo is a no-op when nothing changed, and copy_if_different
# keeps an unchanged archive's timestamp, so nothing relinks.

include_guard(GLOBAL)

# cargo, found the way the rest of the repository finds it: RUST_CARGO, RUST_ENV.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/RustToolchain.cmake)

# The Rust target triples for this build; more than one only for a universal
# macOS build.
function(_ni_rust_triples out)
    set(triples "")
    if (APPLE)
        set(archs ${CMAKE_OSX_ARCHITECTURES})
        if (NOT archs)
            set(archs ${CMAKE_HOST_SYSTEM_PROCESSOR})
        endif()
        foreach(arch IN LISTS archs)
            if (arch STREQUAL "arm64")
                list(APPEND triples aarch64-apple-darwin)
            elseif (arch STREQUAL "x86_64")
                list(APPEND triples x86_64-apple-darwin)
            else()
                message(FATAL_ERROR "no Rust target for macOS architecture '${arch}'")
            endif()
        endforeach()
    elseif (WIN32)
        if (NOT MSVC)
            message(FATAL_ERROR "Windows builds use the MSVC ABI (cl or clang-cl); "
                                "this compiler is ${CMAKE_CXX_COMPILER_ID} without it")
        endif()
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" arch)
        if (arch MATCHES "^(amd64|x86_64|x64)$")
            set(triples x86_64-pc-windows-msvc)
        elseif (arch MATCHES "^(arm64|aarch64)$")
            set(triples aarch64-pc-windows-msvc)
        else()
            message(FATAL_ERROR "no Rust target for Windows processor '${CMAKE_SYSTEM_PROCESSOR}'")
        endif()
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
            set(triples x86_64-unknown-linux-gnu)
        elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
            set(triples aarch64-unknown-linux-gnu)
        else()
            message(FATAL_ERROR "no Rust target for Linux processor '${CMAKE_SYSTEM_PROCESSOR}'")
        endif()
    else()
        # A cross-compiled Linux build included: Linux is built natively, in
        # tools/docker/linux, so its triple is always the host's.
        message(FATAL_ERROR "no Rust target for this ${CMAKE_SYSTEM_NAME} build "
                            "(cross-compiling: ${CMAKE_CROSSCOMPILING})")
    endif()
    set(${out} ${triples} PARENT_SCOPE)
endfunction()

function(ni_add_rust_staticlib name)
    cmake_parse_arguments(ARG "" "MANIFEST;LIB" "INCLUDE" ${ARGN})
    if (NOT ARG_MANIFEST OR NOT ARG_LIB)
        message(FATAL_ERROR "ni_add_rust_staticlib(${name}) needs MANIFEST and LIB")
    endif()

    _ni_rust_triples(triples)
    set(target_dir ${CMAKE_BINARY_DIR}/cargo)
    set(cargo_args build --release --locked --manifest-path ${ARG_MANIFEST} --target-dir ${target_dir})
    foreach(triple IN LISTS triples)
        list(APPEND cargo_args --target ${triple})
    endforeach()

    # What the Rust standard library links against on each platform, as
    # `rustc --print native-static-libs` lists it for the pinned toolchain.
    # macOS needs nothing the compiler driver does not already add (-lSystem).
    set(system_libs "")
    if (WIN32)
        set(archive ${ARG_LIB}.lib)
        set(system_libs kernel32 advapi32 ntdll userenv ws2_32 dbghelp)

        # ONE C RUNTIME PER BINARY. Rust builds for the DLL runtime unless told
        # otherwise; a C++ build on the static one (CMAKE_MSVC_RUNTIME_LIBRARY
        # without "DLL") needs the Rust objects built for it as well, or the
        # link pulls in both libcmt and msvcrt. Unset means CMake's default,
        # which is the DLL runtime.
        if (CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
            list(APPEND cargo_args --config
                 "target.${triples}.rustflags=[\"-C\", \"target-feature=+crt-static\"]")
        endif()
    else()
        set(archive lib${ARG_LIB}.a)
        if (NOT APPLE)
            # -lgcc_s -lutil -lrt -lpthread -lm -ldl -lc; the driver adds -lc.
            set(THREADS_PREFER_PTHREAD_FLAG ON)
            find_package(Threads REQUIRED)
            set(system_libs gcc_s util rt Threads::Threads m ${CMAKE_DL_LIBS})
        endif()
    endif()

    set(slices "")
    foreach(triple IN LISTS triples)
        list(APPEND slices ${target_dir}/${triple}/release/${archive})
    endforeach()
    set(out ${CMAKE_BINARY_DIR}/rust-lib/${archive})
    list(LENGTH slices n)
    if (n GREATER 1)
        set(combine COMMAND lipo -create ${slices} -output ${out}.new)
    else()
        set(combine COMMAND ${CMAKE_COMMAND} -E copy ${slices} ${out}.new)
    endif()

    add_custom_target(${name}_cargo
        BYPRODUCTS ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/rust-lib
        COMMAND ${RUST_ENV} ${RUST_CARGO} ${cargo_args}
        ${combine}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${out}.new ${out}
        COMMENT "cargo: ${ARG_LIB} for ${triples}"
        VERBATIM)

    add_library(${name} INTERFACE)
    target_link_libraries(${name} INTERFACE ${out} ${system_libs})
    if (ARG_INCLUDE)
        target_include_directories(${name} INTERFACE ${ARG_INCLUDE})
    endif()
    add_dependencies(${name} ${name}_cargo)
endfunction()

function(ni_add_rust_test name)
    cmake_parse_arguments(ARG "" "MANIFEST" "" ${ARGN})
    _ni_rust_triples(triples)
    # A universal build tests the slice this machine runs natively.
    if (APPLE AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(triple aarch64-apple-darwin)
    elseif (APPLE)
        set(triple x86_64-apple-darwin)
    else()
        set(triple ${triples})
    endif()
    add_test(NAME ${name}
        COMMAND ${RUST_ENV} ${RUST_CARGO} test --locked --manifest-path ${ARG_MANIFEST}
                --target ${triple} --target-dir ${CMAKE_BINARY_DIR}/cargo)
endfunction()

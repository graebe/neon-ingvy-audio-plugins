# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# THE SINGLE FILTERS: the engine, the shell and the nine bundles, tested once
# for the family (cmake/NiTest.cmake): a family test is each product's, so
# `scripts/test.sh product high-pass` runs all of them, and each bundle's own
# host test is its product's.
#
#   sf_core_rs    the engine's claims, cargo's: every product's table frozen,
#                 its sound its design at any block size, no clicks, its
#                 lanes, the panic, the picture against the played response,
#                 no allocation                                       (quick)
#   sf_move_rs    the Move's module, all nine: their knob grids the
#                 engine's tables, the defaults, the panic, no allocation
#                                                                     (quick)
#   sf_core       the C ABI from C, linked to the plugins' archive    (quick)
#   sf_processor  the processor on the real engine, all nine         (quick)
#   sf_rt         its audio callback allocates nothing               (quick)
#   sf_render_ab  the float and int16 paths within 1.5 LSB, and pinned (full)
#   sf_soak_rs    nine minutes of noise with NaN, ±Inf and denormals  (full)
#   sf_host_<p>   the built NI<Name>.vst3 as a DAW hosts it           (full)

set(SF_ENGINE ${CMAKE_SOURCE_DIR}/engines/single-filter)
set(NI_SF ${CMAKE_SOURCE_DIR}/plugins/_shared/single-filter)

add_test(NAME sf_core_rs
         COMMAND ${RUST_ENV} ${RUST_CARGO} test --manifest-path
                 ${CMAKE_SOURCE_DIR}/Cargo.toml -p sf-core -p sf-capi)
# The Move's module crate and the nine that export it, as they ship.
add_test(NAME sf_move_rs
         COMMAND ${RUST_ENV} ${RUST_CARGO} test --manifest-path ${CMAKE_SOURCE_DIR}/Cargo.toml -p sf-move
                 -p sf-move-high-pass -p sf-move-low-pass -p sf-move-band-pass -p sf-move-notch -p sf-move-bell
                 -p sf-move-low-shelf -p sf-move-high-shelf -p sf-move-tilt -p sf-move-all-pass)
add_test(NAME sf_soak_rs
         COMMAND ${RUST_ENV} ${RUST_CARGO} test --release --manifest-path
                 ${CMAKE_SOURCE_DIR}/Cargo.toml -p sf-core --test soak -- --ignored)

add_executable(sf_core_tests ${SF_ENGINE}/tests/test_core.c)
target_link_libraries(sf_core_tests PRIVATE sf_engine)
add_executable(sf_render_ab ${SF_ENGINE}/tests/render_ab.c)
target_link_libraries(sf_render_ab PRIVATE sf_engine)
if (NOT WIN32)
    target_link_libraries(sf_core_tests PRIVATE m)
    target_link_libraries(sf_render_ab PRIVATE m)
endif()
add_test(NAME sf_core COMMAND sf_core_tests)
add_test(NAME sf_render_ab COMMAND sf_render_ab)

# What ni_add_juce_plugin gives the plugins' sources: a name and a product.
# The shell is the nine's; these programs stand in for any of them.
set(_sf_definitions
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WEBP=0
    "JucePlugin_Name=\"NI Single Filter\"" JucePlugin_WantsMidiInput=0
    "NI_PRODUCT=\"high-pass\"")

juce_add_console_app(sf_processor PRODUCT_NAME sf_processor)
target_sources(sf_processor PRIVATE sf_processor.cpp ${_ni_shell_sources} ${NI_SF}/SingleFilter.cpp)
target_include_directories(sf_processor PRIVATE ${NI_SF} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
target_include_directories(sf_processor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
target_include_directories(sf_processor PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(sf_processor PRIVATE "NI_FIXTURES=\"${CMAKE_SOURCE_DIR}/tests/fixtures\"")
target_compile_definitions(sf_processor PRIVATE ${_sf_definitions}
    JUCE_MODAL_LOOPS_PERMITTED=1 "NI_VERSION_STRING=\"sf_processor\"")
target_link_libraries(sf_processor PRIVATE
    sf_engine ni_editor_single_filter juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME sf_processor COMMAND sf_processor)

if (APPLE)
    juce_add_console_app(sf_rt PRODUCT_NAME sf_rt)
    target_sources(sf_rt PRIVATE sf_rt.cpp ${_ni_shell_sources} ${NI_SF}/SingleFilter.cpp)
    target_include_directories(sf_rt PRIVATE ${NI_SF} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
    target_compile_definitions(sf_rt PRIVATE ${_sf_definitions}
        JUCE_MODAL_LOOPS_PERMITTED=1 "NI_VERSION_STRING=\"sf_rt\"")
    target_link_libraries(sf_rt PRIVATE
        sf_engine ni_editor_single_filter juce::juce_audio_processors
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_dependencies(sf_rt ni_alloc_guard)
    add_test(NAME sf_rt COMMAND sf_rt)
    set_tests_properties(sf_rt PROPERTIES
        ENVIRONMENT "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ni_alloc_guard>")
endif()

# EACH BUILT BUNDLE, AS A DAW HOSTS IT: one program, a test a bundle.
juce_add_console_app(sf_host PRODUCT_NAME sf_host)
target_sources(sf_host PRIVATE sf_host.cpp)
target_compile_definitions(sf_host PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(sf_host PRIVATE
    juce::juce_audio_processors juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
set(_sf_hosts)
get_property(_ni_juce GLOBAL PROPERTY NI_JUCE_BUNDLES)
foreach(bundle IN LISTS _ni_juce)
    get_property(_product GLOBAL PROPERTY NI_JUCE_PRODUCT_${bundle})
    if (_product IN_LIST NI_SINGLE_FILTERS)
        get_property(_name GLOBAL PROPERTY NI_JUCE_NAME_${bundle})
        add_test(NAME sf_host_${_product} COMMAND sf_host ${NI_BUNDLES}/${bundle}.vst3 "${_name}")
        list(APPEND _sf_hosts PRODUCT ${_product} FULL sf_host_${_product}:host)
    endif()
endforeach()

ni_test_tiers(
    PRODUCT single-filter
        PROGRAMS sf_core_tests sf_render_ab sf_processor sf_rt sf_host
        QUICK sf_core_rs sf_move_rs sf_core sf_processor sf_rt
        FULL sf_render_ab:render sf_soak_rs:soak
    ${_sf_hosts})

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The native UI's tests: the kit's, each editor's, and the token guard.
#
#   ni_ui_tests                the kit: every tests/ui/*.cpp that is not an
#                              editor's
#   ni_ui_tests_<product>      one editor: tests/ui/<product>_*.cpp, with
#                              <product> as the plugin's directory is named
#                              (trance-gate_*.cpp, side-chain_*.cpp,
#                              spectrogram_*.cpp, listen-in_*.cpp), registered
#                              once the first such file exists
#
# ONE PROGRAM PER EDITOR, AND ONE FOR THE KIT, so a half-written editor breaks
# its own tests and nobody else's: four editors are written at once, each by
# someone building only the program that is theirs. A kit test that happens to
# carry an editor's prefix still runs, in that editor's program, which links
# the kit too.
#
# DOCTEST, NOT juce::UnitTest. It is vendored at external/doctest and every
# other C++ test here already runs on it (tests/cpp): one framework, one
# output, one way to filter. A test suite then splits a program into the two
# tiers without a second program -- a test case in the suite "snapshot"
# (NI_SNAPSHOT_TEST, snapshot.h) is a golden, and runs in the full tier; the
# rest are unit tests and run in both.

set(DOCTEST_DIR ${NI_ROOT}/external/doctest)
if (NOT EXISTS ${DOCTEST_DIR}/doctest.h)
    message(FATAL_ERROR "external/doctest/doctest.h is missing -- it is vendored, not fetched.")
endif()

# What every test program has: the main that starts JUCE, the snapshot
# harness, the fakes, and the gallery's pages (a page is a ready-made scene
# for a snapshot).
add_library(ni_ui_test_support INTERFACE)
target_sources(ni_ui_test_support INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/snapshot.cpp)
target_include_directories(ni_ui_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# doctest's header is not held to JUCE's warning flags; the tests are.
target_include_directories(ni_ui_test_support SYSTEM INTERFACE ${DOCTEST_DIR})
target_compile_definitions(ni_ui_test_support INTERFACE
    # A test may run the message loop for a while to let a timer fire
    # (MessageManager::runDispatchLoopUntil). Tests only: the kit itself never
    # runs a modal loop, and a plugin build does not define this.
    JUCE_MODAL_LOOPS_PERMITTED=1
    # Baselines are committed beside the tests; what a failed comparison
    # writes stays in the build tree.
    "NI_UI_BASELINES=\"${CMAKE_CURRENT_SOURCE_DIR}/baselines\""
    "NI_UI_SNAPSHOT_OUT=\"${CMAKE_CURRENT_BINARY_DIR}/snapshots\""
    "NI_ROOT=\"${NI_ROOT}\"")
target_link_libraries(ni_ui_test_support INTERFACE
    ni_ui_gallery_pages
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ni_ui_add_tests(<program> <sources>... [LINK <targets>...])
function(ni_ui_add_tests program)
    cmake_parse_arguments(T "" "" "LINK" ${ARGN})
    juce_add_console_app(${program} PRODUCT_NAME ${program})
    target_sources(${program} PRIVATE ${T_UNPARSED_ARGUMENTS})
    target_link_libraries(${program} PRIVATE ni_ui_test_support ${T_LINK})
    # Two entries, one program: the unit tests in both tiers, the goldens in
    # the full one.
    add_test(NAME ${program}_unit
             COMMAND ${program} --test-suite-exclude=snapshot)
    add_test(NAME ${program}_snapshots
             COMMAND ${program} --test-suite=snapshot)
endfunction()

file(GLOB _kit CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(REMOVE_ITEM _kit
     ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/snapshot.cpp)

# An editor's program is its product's; the kit's is shared.
set(_editors)
foreach(product IN LISTS NI_PRODUCTS)
    string(REPLACE "-" "_" id ${product})
    file(GLOB _mine CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${product}_*.cpp)
    if (_mine)
        list(REMOVE_ITEM _kit ${_mine})
        ni_ui_add_tests(ni_ui_tests_${id} ${_mine} LINK ni_editor_${id})
    endif()
    # Listed whether or not they exist yet: the tiers allow a listed test that
    # is not registered, and an editor's first test file needs no edit here.
    list(APPEND _editors PRODUCT ${product}
        PROGRAMS ni_ui_tests_${id}
        QUICK ni_ui_tests_${id}_unit
        FULL ni_ui_tests_${id}_snapshots:snapshot)
endforeach()

ni_ui_add_tests(ni_ui_tests ${_kit})

# THE TOKEN GUARD, which needs node and no build: the generated tokens are
# current, and no kit or editor source spells a colour. Skipped -- never
# failed -- without node, as the root build skips its JavaScript tests.
find_program(NI_NODE node)
if (NI_NODE)
    add_test(NAME ui_tokens_native
             COMMAND ${NI_NODE} --test ${NI_ROOT}/tests/ui_tokens_native.test.mjs)
else()
    message(STATUS "node not found -- the native token guard (ui_tokens_native) is not registered")
endif()

ni_test_tiers(
    PRODUCT shared
        PROGRAMS ni_ui_tests
        QUICK ni_ui_tests_unit ui_tokens_native
        FULL ni_ui_tests_snapshots:snapshot
    ${_editors})

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The test tiers: every registered test is QUICK or FULL, and says which.
#
#   quick  the developer loop -- unit tests, the wire/state/parameter tests,
#          the oracles and the lint-like checks. No bundle, no host, no
#          browser, no timing.                       `ctest -L quick`
#   full   final verification: everything quick, plus what needs a bundle, a
#          host, a second process, a browser or the coverage build.
#                                                    `ctest -L full`
#
# docs/tech/testing.md has the whole picture; scripts/test.sh is the entry
# point.
#
#   ni_test_tiers(QUICK <test>... FULL <test>:<why>...)
#
# Called once at the end of a directory's CMakeLists.txt, for the tests that
# directory registered. A quick test is labelled `quick;full`, so `-L full` is
# literally everything; a full-only test is labelled `full;<why>` -- render,
# host, ipc, e2e, coverage ... -- which says what keeps it out of quick and
# lets one kind be run alone (`ctest -L e2e`).
#
# EXHAUSTIVE, AND CHECKED. A test the directory registered but did not list
# stops the configure: a new test must be placed in a tier by whoever adds it,
# rather than defaulting into quick the day it opens a bundle. A listed test
# that is not registered is fine -- several are conditional (no node, no
# Rosetta, not the coverage build).
#
# Per directory rather than from one place because set_tests_properties only
# reaches another directory's tests from CMake 3.28, and this project asks for
# 3.22.
function(ni_test_tiers)
    cmake_parse_arguments(T "" "" "QUICK;FULL" ${ARGN})
    get_property(registered DIRECTORY PROPERTY TESTS)

    set(listed)
    foreach(t ${T_QUICK})
        list(APPEND listed ${t})
        if (t IN_LIST registered)
            set_tests_properties(${t} PROPERTIES LABELS "quick;full")
        endif()
    endforeach()
    foreach(entry ${T_FULL})
        string(REPLACE ":" ";" pair "${entry}")
        list(LENGTH pair n)
        if (NOT n EQUAL 2)
            message(FATAL_ERROR "ni_test_tiers: FULL takes <test>:<why>, got '${entry}'")
        endif()
        list(GET pair 0 t)
        list(GET pair 1 why)
        list(APPEND listed ${t})
        if (t IN_LIST registered)
            set_tests_properties(${t} PROPERTIES LABELS "full;${why}")
        endif()
    endforeach()

    foreach(t ${registered})
        if (NOT t IN_LIST listed)
            message(FATAL_ERROR
                "test '${t}' (${CMAKE_CURRENT_SOURCE_DIR}) is in no tier. Add it to "
                "the ni_test_tiers() call at the end of that CMakeLists.txt: QUICK if "
                "it needs no bundle, host, browser or second process, FULL otherwise "
                "-- see cmake/NiTest.cmake.")
        endif()
    endforeach()
endfunction()

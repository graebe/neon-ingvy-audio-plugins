# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The test tiers and the products: every registered test is QUICK or FULL,
# belongs to ONE product or is shared, and says which.
#
#   quick  the developer loop -- unit tests, the wire/state/parameter tests,
#          the oracles and the lint-like checks. No bundle, no host, no
#          browser, no timing.                       `ctest -L quick`
#   full   final verification: everything quick, plus what needs a bundle, a
#          host, a second process, a browser or the coverage build.
#                                                    `ctest -L full`
#
#   product:<product>  a test of that product alone: its engine, its
#          processor, its editor, its Move module, its built bundle -- a
#          shared check included, when what it opens is that product's bundle
#   product:shared     a test of what every product is built on: the shared
#          engines (audio-bus, ground, the shell, engines/shared), the native
#          kit, the C++ helpers, and the checks over the tree
#
# A product's release builds and runs its own tests and the shared ones, and
# nobody else's: `ctest -L '^product:(<product>|shared)$'`, which is
# `scripts/test.sh product <product>`. docs/tech/testing.md has the whole
# picture; scripts/test.sh is the entry point.
#
#   ni_test_tiers(
#       PRODUCT <product>|shared
#           [PROGRAMS <target>...]
#           [QUICK <test>...]
#           [FULL <test>:<why>...]
#       PRODUCT ...)
#
# Called once at the end of a directory's CMakeLists.txt, for the tests and
# the programs that directory made. <product> is one of the root build's
# NI_PRODUCTS; one may head several sections. A quick test is labelled
# `quick;full`, so `-L full` is literally everything; a full-only test is
# labelled `full;<why>` -- render, host, ipc, e2e, coverage ... -- which says
# what keeps it out of quick and lets one kind be run alone (`ctest -L e2e`).
# Each also carries `product:<product>`.
#
# PROGRAMS are the targets the section's tests run, and `ni_tests_<product>`
# (ni_test_targets) builds exactly those -- so a product's run builds its own
# test programs and the shared ones, and none of another product's.
# What a program links or depends on is built with it.
#
# EXHAUSTIVE, AND CHECKED. A test the directory registered but did not place,
# or placed twice, stops the configure, and so does a program it built and
# did not place: a new test must be put in a tier and a product by whoever
# adds it, rather than defaulting into quick the day it opens a bundle, or
# into every product's release. A listed test or program that does not exist
# is fine -- several are conditional (no node, no Rosetta, not the coverage
# build, not macOS).
#
# Per directory rather than from one place because set_tests_properties only
# reaches another directory's tests from CMake 3.28, and this project asks for
# 3.22.

# One test into its tiers and its product, once: in ni_test_tiers' scope.
macro(_ni_test_place t tiers)
    if ("${t}" IN_LIST placed)
        message(FATAL_ERROR "test '${t}' (${CMAKE_CURRENT_SOURCE_DIR}) is placed twice: "
                            "one tier, one product")
    endif()
    list(APPEND placed ${t})
    if ("${t}" IN_LIST registered)
        set_tests_properties(${t} PROPERTIES LABELS "${tiers};product:${product}")
    endif()
endmacro()

function(ni_test_tiers)
    get_property(registered DIRECTORY PROPERTY TESTS)
    get_property(built DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)

    # The sections, in order: each PRODUCT <name> and what follows it.
    set(n 0)
    set(expect_name FALSE)
    foreach(arg IN LISTS ARGN)
        if (expect_name)
            if (NOT (arg STREQUAL "shared" OR arg IN_LIST NI_PRODUCTS))
                message(FATAL_ERROR "ni_test_tiers: PRODUCT '${arg}' is neither 'shared' "
                                    "nor one of NI_PRODUCTS (${NI_PRODUCTS})")
            endif()
            set(product_${n} ${arg})
            set(args_${n})
            set(expect_name FALSE)
        elseif (arg STREQUAL "PRODUCT")
            math(EXPR n "${n} + 1")
            set(expect_name TRUE)
        elseif (n EQUAL 0)
            message(FATAL_ERROR "ni_test_tiers: every test and program is in a "
                                "PRODUCT section, and '${arg}' comes before the first")
        else()
            list(APPEND args_${n} ${arg})
        endif()
    endforeach()
    if (expect_name)
        message(FATAL_ERROR "ni_test_tiers: PRODUCT with no product after it")
    endif()

    set(placed)
    set(programs)
    if (n GREATER 0)
        foreach(i RANGE 1 ${n})
            set(product ${product_${i}})
            cmake_parse_arguments(T "" "" "PROGRAMS;QUICK;FULL" ${args_${i}})
            if (T_UNPARSED_ARGUMENTS)
                message(FATAL_ERROR "ni_test_tiers: PRODUCT ${product} has '${T_UNPARSED_ARGUMENTS}' "
                                    "outside PROGRAMS, QUICK and FULL")
            endif()

            foreach(t IN LISTS T_QUICK)
                _ni_test_place(${t} "quick;full")
            endforeach()
            foreach(entry IN LISTS T_FULL)
                string(REPLACE ":" ";" pair "${entry}")
                list(LENGTH pair len)
                if (NOT len EQUAL 2)
                    message(FATAL_ERROR "ni_test_tiers: FULL takes <test>:<why>, got '${entry}'")
                endif()
                list(GET pair 0 t)
                list(GET pair 1 why)
                _ni_test_place(${t} "full;${why}")
            endforeach()

            foreach(p IN LISTS T_PROGRAMS)
                if (p IN_LIST programs)
                    message(FATAL_ERROR "program '${p}' (${CMAKE_CURRENT_SOURCE_DIR}) is placed "
                                        "in two products")
                endif()
                list(APPEND programs ${p})
                if (p IN_LIST built)
                    set_property(GLOBAL APPEND PROPERTY NI_TEST_PROGRAMS_${product} ${p})
                endif()
            endforeach()
        endforeach()
    endif()

    foreach(t IN LISTS registered)
        if (NOT t IN_LIST placed)
            message(FATAL_ERROR
                "test '${t}' (${CMAKE_CURRENT_SOURCE_DIR}) is in no tier and no product. "
                "Add it to the ni_test_tiers() call at the end of that CMakeLists.txt, in "
                "the PRODUCT section it tests (shared for what every product is built "
                "on): QUICK if it needs no bundle, host, browser or second process, FULL "
                "otherwise -- see cmake/NiTest.cmake.")
        endif()
    endforeach()
    foreach(p IN LISTS built)
        get_target_property(type ${p} TYPE)
        if (NOT type STREQUAL "INTERFACE_LIBRARY" AND NOT p IN_LIST programs)
            message(FATAL_ERROR
                "program '${p}' (${CMAKE_CURRENT_SOURCE_DIR}) is in no product. Add it to "
                "PROGRAMS in the PRODUCT section of the ni_test_tiers() call at the end of "
                "that CMakeLists.txt whose tests run it -- see cmake/NiTest.cmake.")
        endif()
    endforeach()
endfunction()

# What builds the tests, once every directory has placed its programs: called
# at the end of tests/CMakeLists.txt.
#
#   ni_tests_<product>  that product's test programs (product:<product>)
#   ni_tests_shared     the shared ones (product:shared)
#   ni_tests            all of them
#
# None builds a plugin bundle; a test that hosts one needs its <Bundle>_VST3
# too, which is what `scripts/test.sh product` adds.
function(ni_test_targets)
    add_custom_target(ni_tests)
    foreach(product shared ${NI_PRODUCTS})
        get_property(programs GLOBAL PROPERTY NI_TEST_PROGRAMS_${product})
        add_custom_target(ni_tests_${product})
        if (programs)
            add_dependencies(ni_tests_${product} ${programs})
        endif()
        add_dependencies(ni_tests ni_tests_${product})
    endforeach()
endfunction()

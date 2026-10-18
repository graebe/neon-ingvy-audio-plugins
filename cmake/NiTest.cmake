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
#          engines (audio-bus, the shell, engines/shared), the native kit,
#          the C++ helpers, and the checks over the tree
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
# A PRODUCT FAMILY is products built on one engine and one shell -- the
# single filters, nine products on engines/single-filter -- whose shared tests
# are each member's: declared once (ni_product_family), a section headed by
# the family's name labels its tests product:<member> for every member and
# puts its programs in every member's ni_tests_<member>, so `scripts/test.sh
# product high-pass` runs the engine and the shell the nine share.
#
# Per directory rather than from one place because set_tests_properties only
# reaches another directory's tests from CMake 3.28, and this project asks for
# 3.22.

# ni_product_family(<family> <product>...): the products one family's tests
# belong to. Each is one of NI_PRODUCTS; the family's name is no product.
function(ni_product_family family)
    foreach(p IN LISTS ARGN)
        if (NOT p IN_LIST NI_PRODUCTS)
            message(FATAL_ERROR "ni_product_family(${family}): '${p}' is not one of NI_PRODUCTS")
        endif()
    endforeach()
    set_property(GLOBAL APPEND PROPERTY NI_PRODUCT_FAMILIES ${family})
    set_property(GLOBAL PROPERTY NI_PRODUCT_FAMILY_${family} ${ARGN})
endfunction()

# One test into its tiers and its products, once: in ni_test_tiers' scope.
macro(_ni_test_place t tiers)
    if ("${t}" IN_LIST placed)
        message(FATAL_ERROR "test '${t}' (${CMAKE_CURRENT_SOURCE_DIR}) is placed twice: "
                            "one tier, one product")
    endif()
    list(APPEND placed ${t})
    if ("${t}" IN_LIST registered)
        set(_labels ${tiers})
        foreach(_member IN LISTS members)
            list(APPEND _labels product:${_member})
        endforeach()
        set_tests_properties(${t} PROPERTIES LABELS "${_labels}")
    endif()
endmacro()

function(ni_test_tiers)
    get_property(registered DIRECTORY PROPERTY TESTS)
    get_property(built DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    get_property(families GLOBAL PROPERTY NI_PRODUCT_FAMILIES)

    # The sections, in order: each PRODUCT <name> and what follows it.
    set(n 0)
    set(expect_name FALSE)
    foreach(arg IN LISTS ARGN)
        if (expect_name)
            if (NOT (arg STREQUAL "shared" OR arg IN_LIST NI_PRODUCTS OR arg IN_LIST families))
                message(FATAL_ERROR "ni_test_tiers: PRODUCT '${arg}' is neither 'shared', "
                                    "one of NI_PRODUCTS (${NI_PRODUCTS}) nor a family (${families})")
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
            if (product IN_LIST families)
                get_property(members GLOBAL PROPERTY NI_PRODUCT_FAMILY_${product})
            else()
                set(members ${product})
            endif()
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
                    foreach(m IN LISTS members)
                        set_property(GLOBAL APPEND PROPERTY NI_TEST_PROGRAMS_${m} ${p})
                    endforeach()
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

# WHAT THIS BUILD CANNOT TEST, SAID ONCE AND KEPT. A test that needs something
# the machine does not have -- node, Docker, Rosetta -- is not registered, and
# the configure says so in a line that scrolls away. Each such gap is recorded
# here instead, and ni_test_targets writes them all to
# <build>/ni-capabilities.txt, one per line: `<name> <what is not tested>`.
# scripts/test.sh reads it: the full tier refuses to call itself green while a
# capability is missing, unless the run waives it by name (--waive <name>).
function(ni_missing_capability name what)
    set_property(GLOBAL APPEND PROPERTY NI_MISSING_CAPABILITIES "${name} ${what}")
    message(STATUS "${name} not found -- ${what}")
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
    get_property(missing GLOBAL PROPERTY NI_MISSING_CAPABILITIES)
    set(text "")
    foreach(line IN LISTS missing)
        string(APPEND text "${line}\n")
    endforeach()
    file(WRITE ${CMAKE_BINARY_DIR}/ni-capabilities.txt "${text}")

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

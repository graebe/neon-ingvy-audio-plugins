# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The plain C++ the processors share, on doctest, with no JUCE and no engine:
# ni::wire and ni::Scope (plugins/_shared/ni). A product's processor, its
# model and its editor are tested where JUCE is linked: tests/tg_processor.cpp
# and tests/<product>/.
#
# doctest is MIT and one header, vendored at external/doctest. Nothing that
# ships links it, so THIRD_PARTY_LICENSES.md lists it as test-only.

set(DOCTEST_DIR ${CMAKE_SOURCE_DIR}/external/doctest)

if (NOT EXISTS ${DOCTEST_DIR}/doctest.h)
    message(FATAL_ERROR
        "external/doctest/doctest.h is missing -- it is vendored, not fetched. "
        "See THIRD_PARTY_LICENSES.md for the version and provenance.")
endif()

set(NI_SHARED ${CMAKE_SOURCE_DIR}/plugins/_shared)

# What every processor does to a block, a clock or a number: the quantisers,
# the transport, the chunking, the scope capture and the numbers -- the last
# under a comma-decimal locale too.
add_executable(ni_wire ni_wire.cpp ${NI_SHARED}/ni/Wire.cpp)
target_include_directories(ni_wire PRIVATE ${DOCTEST_DIR} ${NI_SHARED})
add_test(NAME ni_wire COMMAND ni_wire)

# Quick: a doctest binary over plain C++, with no bundle, host or second
# process; shared, as the code is. cmake/NiTest.cmake.
ni_test_tiers(PRODUCT shared PROGRAMS ni_wire QUICK ni_wire)

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The cross-cutting suite. Every C test links the same engine archives the
# plugins link (cmake/NiRust.cmake), so what passes here is what ships; the
# plain-C tests against each engine's ABI are what check the SOUND.
#
# -DNI_SANITIZE=thread|address|undefined instruments every C/C++ target in this
# directory and tests/cpp -- the tests, never the plugins. cmake/Sanitize.cmake.
ni_sanitize_here()

# The Rust suites: ni_cargo_test, rust_tests_build and rust_doctests.
include(${CMAKE_SOURCE_DIR}/cmake/NiCargoTest.cmake)

set(TG_ENGINE ${CMAKE_SOURCE_DIR}/engines/trance-gate)

add_executable(tg_core_tests ${TG_ENGINE}/tests/test_core.c)
target_link_libraries(tg_core_tests PRIVATE tg_engine)
if (NOT WIN32)
    target_link_libraries(tg_core_tests PRIVATE m)
endif()
add_test(NAME tg_core COMMAND tg_core_tests)

# Renders through the PLUGIN's audio path (float, split channels, a DAW-shaped
# transport) and compares against the Move module's reference render. This is
# the check that the port did not change the sound, and it has survived the
# engine going to Rust and the shell going from JUCE to iPlug2 unchanged --
# which is the point of it being plain C against the ABI.
add_executable(tg_render_plugin render_plugin.c)
target_link_libraries(tg_render_plugin PRIVATE tg_engine)
if (NOT WIN32)
    target_link_libraries(tg_render_plugin PRIVATE m)
endif()
add_test(NAME tg_render_ab COMMAND tg_render_plugin --verify)

# THE CURVE ORACLE. The engine's shape() against a table it generated, so the
# curve maths changes only when it is meant to: the web editor's copy of it had
# the S-curve's first half un-mirrored for as long as it existed, and nothing
# caught it, because a wrong curve is still a curve. The native editor draws
# what the engine renders (tg_core_render_envelope), so the engine is the one
# copy left to pin.
#
# Regenerate the fixture with `tg_shape_table > <the file below>`, and only
# when the curve maths is MEANT to change.
set(TG_FIXTURES ${TG_ENGINE}/tests/fixtures)

add_executable(tg_shape_table shape_table.c)
target_link_libraries(tg_shape_table PRIVATE tg_engine)
if (NOT WIN32)
    target_link_libraries(tg_shape_table PRIVATE m)
endif()
add_test(NAME tg_curves COMMAND tg_shape_table --verify ${TG_FIXTURES}/shape_table.txt)

# THE ENVELOPE ORACLE, and it is MEASURED rather than transcribed.
#
# The curve table above pins shape(); this pins the whole stage machine around
# it -- when the gate shuts, what the release falls from, what a zero-length
# stage does, and what the attack starts from when the previous release is
# still sounding. tg_envelope_table runs the REAL engine with a DC input at
# amount 1, where the gain it applies IS the envelope, so the reference owes
# nothing to anyone's reading of the Rust.
add_executable(tg_envelope_table envelope_table.c)
target_link_libraries(tg_envelope_table PRIVATE tg_engine)
if (NOT WIN32)
    target_link_libraries(tg_envelope_table PRIVATE m)
endif()
add_test(NAME tg_envelope COMMAND tg_envelope_table --verify ${TG_FIXTURES}/envelope_table.txt)

# THE FADE ORACLE, measured the same way: DC in, no envelope, so the gain
# during a step IS that step's arrival weight. If the weight table and the
# gain law ever disagree, this reports the gain law -- the one a listener
# hears.
add_executable(tg_fade_table fade_table.c)
target_link_libraries(tg_fade_table PRIVATE tg_engine)
if (NOT WIN32)
    target_link_libraries(tg_fade_table PRIVATE m)
endif()
add_test(NAME tg_fade COMMAND tg_fade_table --verify ${TG_FIXTURES}/fade_table.txt)

# node's OWN test runner (22+) for the checks over the tree. Skipped rather than
# failed when node is absent: a C++ developer building the plugin should not
# need a JS toolchain to run the DSP tests.
find_program(TG_NODE node)
if (TG_NODE)
    # THE MOVE EDITOR, RUN: ui_chain.js against Schwung's own shared modules
    # and the module's real chain_params -- the pads, the paging, the ring and
    # the redraw gate. It needs a Schwung checkout beside this one, which the
    # script finds; without one it exits 77 and ctest reports it Skipped
    # rather than passing it.
    add_test(NAME tg_move_ui COMMAND ${TG_ENGINE}/tests/ui.sh)
    set_tests_properties(tg_move_ui PROPERTIES SKIP_RETURN_CODE 77)

    # ONE VERSION PER PRODUCT, spelled once. A product's version appears in
    # three or four files in three languages, nothing held them together, and
    # they had already come apart: the Spectrogram shipped PLUG_VERSION_STR
    # "1.0.0" against crates that said 0.1.0.
    add_test(NAME versions
             COMMAND ${TG_NODE} --test ${CMAKE_SOURCE_DIR}/tests/versions.test.mjs)
    # And what each built bundle says, which needs it: a JUCE build writes its
    # Info.plist and moduleinfo.json itself, from versions.json. Per bundle,
    # below (versions_bundle_<bundle>).

    # WHAT A RELEASE TAG MEANS. Both release workflows parse their tag with
    # scripts/release.mjs, and a workflow only ever runs on the tag it is
    # about to get wrong -- so this runs the parser, the release.json writer
    # and the workflows' tag lists against the real tree instead.
    add_test(NAME release
             COMMAND ${TG_NODE} --test ${CMAKE_SOURCE_DIR}/tests/release.test.mjs)
    # And the release staging dry-run against each bundle this build made, per
    # bundle below (release_bundle_<bundle>).

    # EVERYTHING THAT SHIPS HAS A NOTICE, AND EVERY NOTICE IS FOR SOMETHING
    # THAT SHIPS. THIRD_PARTY_LICENSES.md is held to the SDK pins, the
    # workspace's crates and what `cargo metadata` says ships from crates.io,
    # the editors' bundled packages and the fonts -- and, through --bundles, to
    # the built bundles themselves, which must carry the notices inside.
    #
    # Two kinds of test, because they need different things built: `licenses`
    # reads the tree and the editors' build output (written at configure
    # time), so it is a quick check; `licenses_bundle_<bundle>` (below) opens a
    # bundle, so it needs that bundle built. RUST_ENV, because the check asks
    # cargo what ships.
    add_test(NAME licenses
             COMMAND ${RUST_ENV} ${TG_NODE} ${CMAKE_SOURCE_DIR}/scripts/check-licenses.mjs)
    # What the check counts as shipping, on a graph small enough to read: the
    # feature resolver it uses instead of cargo metadata's unified features.
    add_test(NAME licenses_resolve
             COMMAND ${TG_NODE} --test ${CMAKE_SOURCE_DIR}/tests/licenses_resolve.test.mjs)

    # EVERY SOURCE FILE SAYS WHOSE IT IS, AND UNDER WHAT LICENCE: the SPDX line
    # and the copyright line open every file this repository owns, in its own
    # comment syntax. What is excluded, and why, is listed in the test.
    add_test(NAME spdx
             COMMAND ${TG_NODE} --test ${CMAKE_SOURCE_DIR}/tests/spdx.test.mjs)

    # THE SCHWUNG VERSIONS A MODULE NAMES, HELD TO EACH OTHER: the pinned
    # version it is tested against (modules/_shared/schwung.version, in the
    # format it documents), module.json's min_host_version, and what the
    # module's help and docs tell the user about both.
    add_test(NAME schwung_versions
             COMMAND ${TG_NODE} --test ${CMAKE_SOURCE_DIR}/tests/schwung_versions.test.mjs)

    # THE SITE'S TOKEN GUARD: no colour spelled outside site/src/uv/tokens.css,
    # no var() it does not define, and the file held to the design system's
    # tokens.json. The native editors' guard is tests/ui's ui_tokens_native.
    add_test(NAME site_tokens
             COMMAND ${TG_NODE} --test ${CMAKE_SOURCE_DIR}/tests/site_tokens.test.mjs)

    # THE TIMING LOG every build and test script appends to (scripts/timing.sh):
    # a record per stage with every field, the wrapped command's status
    # unchanged, the .ninja_log beside it, and scripts/build-timings.py's
    # summary of it. Into a log of its own, never the real one.
    add_test(NAME timing
             COMMAND ${TG_NODE} --test ${CMAKE_SOURCE_DIR}/tests/timing.test.mjs)

    # ONE RUST TOOLCHAIN: rust-toolchain.toml's pin, and the same version in
    # the Move module's and the Linux cross-build's images, each on a base
    # pinned by digest.
    add_test(NAME toolchain
             COMMAND ${TG_NODE} --test ${CMAKE_SOURCE_DIR}/tests/toolchain.test.mjs)

    # NO BUILD READS THE CLOCK, so ccache may ignore __DATE__ and __TIME__
    # (cmake/NiCcache.cmake): no owned source names one.
    add_test(NAME time_macros
             COMMAND ${TG_NODE} --test ${CMAKE_SOURCE_DIR}/tests/time_macros.test.mjs)

    # WHAT A RUN DID NOT TEST: scripts/test-summary.mjs reads ctest's report
    # and the build's missing capabilities, and the full tier's gate fails on
    # any gap not waived by name.
    add_test(NAME test_summary
             COMMAND ${TG_NODE} --test ${CMAKE_SOURCE_DIR}/tests/test_summary.test.mjs)

    # Every path into design/ that a tracked file names exists, however it is
    # spelled: from the root, climbing from the naming file's own directory
    # with ../ (as the icon glob does), or as path.join() arguments. The
    # mirror moves when its artifacts do, and the icon glob and the token and
    # field oracles read through those paths. The test's header has the rules.
    add_test(NAME design_paths
             COMMAND ${TG_NODE} --test
                     ${CMAKE_SOURCE_DIR}/tests/design_paths.test.mjs)

    # Every repository path the documentation names exists: the docs are
    # the site and every agent's contract, and prose does not fail a build.
    add_test(NAME doc_paths
             COMMAND ${TG_NODE} --test
                     ${CMAKE_SOURCE_DIR}/tests/doc_paths.test.mjs)

    # The product matrix: each README's hosts and notOnMove against the
    # modules the tree builds and the ones release.json offers.
    add_test(NAME product_matrix
             COMMAND ${TG_NODE} --test
                     ${CMAKE_SOURCE_DIR}/tests/product_matrix.test.mjs)

    # THE SITE'S OWN RENDER A/B. It is served from /neon-ingvy-audio-plugins/, so a
    # hand-written href="/tech/" works in every local preview -- astro applies
    # the base there too -- and 404s only once deployed, only for whoever clicks
    # it. This walks the built HTML instead of trusting that nobody did it.
    #
    # It needs site/dist, which `npm run build --workspace site` produces; with
    # no build present it says so and skips rather than failing a C++ developer's
    # ctest run.
    add_test(NAME site_links
             COMMAND ${TG_NODE} ${CMAKE_SOURCE_DIR}/site/scripts/check-links.mjs)
    set_tests_properties(site_links PROPERTIES SKIP_RETURN_CODE 2)

    # THE MOVE'S WEB EDITORS (web/, docs/tech/move-web-editors.md): the kit's
    # and each editor's node tests, and each editor built and driven in Chrome
    # against a fake Move with the real engine behind it. scripts/test-web.sh
    # skips (77) without node_modules.
    foreach(_web web_kit:unit:kit web_tg:unit:trance-gate web_sc:unit:side-chain
                 web_tg_e2e:e2e:trance-gate web_sc_e2e:e2e:side-chain)
        string(REPLACE ":" ";" _w ${_web})
        list(GET _w 0 _name)
        list(GET _w 1 _kind)
        list(GET _w 2 _what)
        add_test(NAME ${_name}
                 COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-web.sh ${_kind} ${_what})
        set_tests_properties(${_name} PROPERTIES SKIP_RETURN_CODE 77)
        # A BROWSER SUITE RUNS ALONE. Its sync tests measure the playhead frame
        # by frame, and Chrome starved by the full tier's other work -- four
        # Docker module builds among it -- lost its sessions outright
        # ("Internal server error, session closed"). On its own it is the
        # machine the tests describe.
        if (_kind STREQUAL "e2e")
            set_tests_properties(${_name} PROPERTIES RUN_SERIAL TRUE)
        endif()
    endforeach()
else()
    ni_missing_capability(node "the checks over the tree (versions, licences, tokens, the site) are not registered")
endif()

# The bundles this build wrote: every test that hosts one loads it from here,
# by path, and never looks at what is installed.
set(NI_BUNDLES ${CMAKE_BINARY_DIR}/out)

# ------------------------------------------------------------ the native kit

# The kit's and the editors' own tests (tests/ui): unit tests in quick, the
# snapshot goldens in full. They place themselves in the tiers.
add_subdirectory(ui)

# ------------------------------------------------------------ the JUCE bundles

# EVERY BUNDLE BUILT ON THE JUCE SHELL (cmake/NiJucePlugin.cmake), hosted as a
# DAW hosts it: juce_host.cpp loads the .vst3 from build/out with JUCE's own
# VST3 host, runs it with MIDI, round-trips its state through a second
# instance and opens its editor. And its signature, checked as Live's scanner
# checks it: a bundle JUCE wrote into after signing loads in pluginval and is
# rejected by Live ("a sealed resource is missing or invalid").
juce_add_console_app(juce_host PRODUCT_NAME juce_host)
target_sources(juce_host PRIVATE juce_host.cpp)
target_compile_definitions(juce_host PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    # It lets the editor's frame clock run for a moment: a test, not a plugin.
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(juce_host PRIVATE
    juce::juce_audio_utils juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
#
# In a bus namespace of this build's own (NIA_BUS_NS, engines/audio-bus): a
# product that claims or reads a bus never meets a Live session's, or another
# checkout's. Their tiers follow the list, so a product moving onto the shell
# is tested here by being built.
#
# AND THE CHECKS OVER THE TREE THAT OPEN A BUNDLE, one test per bundle, so a
# product's release opens its own bundle and needs no other built: what its
# Info.plist and moduleinfo.json say (versions_bundle_*), the release staging
# dry-run against it -- its notices and binary where its zip expects them,
# and both slices when the build made both (release_bundle_*) -- and the
# notices inside it (licenses_bundle_*). NI_PRODUCT, or --product, is the one
# product a check looks at. Each belongs to its bundle's product.
if ("arm64" IN_LIST CMAKE_OSX_ARCHITECTURES AND "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
    set(_ni_universal 1)
else()
    set(_ni_universal 0)
endif()
get_property(_ni_juce GLOBAL PROPERTY NI_JUCE_BUNDLES)
set(_ni_juce_tiers)
foreach(bundle IN LISTS _ni_juce)
    get_property(_name GLOBAL PROPERTY NI_JUCE_NAME_${bundle})
    get_property(_product GLOBAL PROPERTY NI_JUCE_PRODUCT_${bundle})
    add_test(NAME juce_host_${bundle} COMMAND juce_host ${NI_BUNDLES}/${bundle}.vst3 "${_name}")
    set_tests_properties(juce_host_${bundle} PROPERTIES ENVIRONMENT "NIA_BUS_NS=${CMAKE_BINARY_DIR}")
    set(_full juce_host_${bundle}:host)
    if (APPLE)
        add_test(NAME codesign_${bundle}
                 COMMAND codesign --verify --deep --strict ${NI_BUNDLES}/${bundle}.vst3)
        list(APPEND _full codesign_${bundle}:bundles)
    endif()
    if (TG_NODE)
        add_test(NAME versions_bundle_${bundle}
                 COMMAND ${TG_NODE} --test ${CMAKE_SOURCE_DIR}/tests/versions.test.mjs)
        add_test(NAME release_bundle_${bundle}
                 COMMAND ${TG_NODE} --test ${CMAKE_SOURCE_DIR}/tests/release.test.mjs)
        set_tests_properties(versions_bundle_${bundle} release_bundle_${bundle} PROPERTIES
            ENVIRONMENT "NI_BUNDLES=${NI_BUNDLES};NI_PRODUCT=${_product};NI_UNIVERSAL=${_ni_universal}")
        add_test(NAME licenses_bundle_${bundle}
                 COMMAND ${RUST_ENV} ${TG_NODE} ${CMAKE_SOURCE_DIR}/scripts/check-licenses.mjs
                         --bundles ${NI_BUNDLES} --product ${_product})
        list(APPEND _full versions_bundle_${bundle}:bundles release_bundle_${bundle}:bundles
                          licenses_bundle_${bundle}:bundles)
    endif()
    list(APPEND _ni_juce_tiers PRODUCT ${_product} FULL ${_full})
endforeach()

# THE JUCE SHELL'S STATE CODEC (plugins/_shared/juce/Nist.h) against every
# iPlug2 fixture of every product -- decoded as captured, written back byte
# for byte -- and the Trance Gate's legacy layouts and refusals on its real
# table. The codec is plain C++; juce_core reads the fixtures' JSON.
set(NI_JUCE_SHELL ${CMAKE_SOURCE_DIR}/plugins/_shared/juce)
set(NI_TG ${CMAKE_SOURCE_DIR}/plugins/trance-gate)
set(NI_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/iplug2)
juce_add_console_app(nist_fixtures PRODUCT_NAME nist_fixtures)
target_sources(nist_fixtures PRIVATE nist_fixtures.cpp
    ${NI_JUCE_SHELL}/Nist.cpp ${NI_TG}/Params.cpp ${CMAKE_SOURCE_DIR}/plugins/_shared/ni/Wire.cpp)
target_include_directories(nist_fixtures PRIVATE ${NI_JUCE_SHELL} ${NI_TG} ${CMAKE_SOURCE_DIR}/plugins/_shared)
target_include_directories(nist_fixtures SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
target_compile_definitions(nist_fixtures PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 "NI_FIXTURES=\"${NI_FIXTURES}\"")
target_link_libraries(nist_fixtures PRIVATE
    tg_engine juce::juce_core juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME nist_fixtures COMMAND nist_fixtures)

# NI TRANCE GATE'S PROCESSOR, ITS MODEL AND ITS EDITOR, IN ONE PROGRAM: the
# product's sources and the shell's compiled in, as the plugin compiles them,
# with the definitions its build gives them, around the real engine.
# tg_processor.cpp says what it holds; tests/ui holds the editor on fakes.
file(GLOB _ni_shell_sources CONFIGURE_DEPENDS ${NI_JUCE_SHELL}/*.cpp)
juce_add_console_app(tg_processor PRODUCT_NAME tg_processor)
target_sources(tg_processor PRIVATE tg_processor.cpp ${_ni_shell_sources}
    ${NI_TG}/TranceGate.cpp ${NI_TG}/Params.cpp ${NI_TG}/EngineModel.cpp ${NI_TG}/Window.cpp
    ${CMAKE_SOURCE_DIR}/plugins/_shared/ni/Wire.cpp)
target_include_directories(tg_processor PRIVATE
    ${NI_TG} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_include_directories(tg_processor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
target_compile_definitions(tg_processor PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WEBP=0
    # A test may run the message loop for a moment to let a timer fire.
    JUCE_MODAL_LOOPS_PERMITTED=1
    "JucePlugin_Name=\"NI Trance Gate\"" JucePlugin_WantsMidiInput=0
    "NI_PRODUCT=\"trance-gate\"" "NI_VERSION_STRING=\"tg_processor\""
    "NI_FIXTURES=\"${NI_FIXTURES}\"")
target_link_libraries(tg_processor PRIVATE
    tg_engine ni_editor_trance_gate juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME tg_processor COMMAND tg_processor)

# NI TRANCE GATE'S AUDIO CALLBACK ALLOCATES NOTHING: the processor under
# alloc_guard.c, which counts every malloc of the calling thread -- C++, JUCE
# and the Rust engine alike. The guard is a dyld interposer, so macOS only;
# the engines' own no-allocation tests (assert_no_alloc) run everywhere.
if (APPLE)
    add_library(ni_alloc_guard SHARED alloc_guard.c)
    juce_add_console_app(tg_rt PRODUCT_NAME tg_rt)
    target_sources(tg_rt PRIVATE tg_rt.cpp ${_ni_shell_sources}
        ${NI_TG}/TranceGate.cpp ${NI_TG}/Params.cpp ${NI_TG}/EngineModel.cpp ${NI_TG}/Window.cpp
        ${CMAKE_SOURCE_DIR}/plugins/_shared/ni/Wire.cpp)
    target_include_directories(tg_rt PRIVATE
        ${NI_TG} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
    target_compile_definitions(tg_rt PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WEBP=0
        "JucePlugin_Name=\"NI Trance Gate\"" JucePlugin_WantsMidiInput=0
        "NI_PRODUCT=\"trance-gate\"" "NI_VERSION_STRING=\"tg_rt\"")
    target_link_libraries(tg_rt PRIVATE
        tg_engine ni_editor_trance_gate juce::juce_audio_processors
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_dependencies(tg_rt ni_alloc_guard)
    add_test(NAME tg_rt COMMAND tg_rt ${NI_FIXTURES})
    set_tests_properties(tg_rt PROPERTIES
        ENVIRONMENT "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ni_alloc_guard>")
endif()

# THE BUILT NITranceGate.vst3, AS A DAW HOSTS IT: the iPlug2 class, the
# parameters through the controller, every fixture reopened as Live reopens a
# set and saved back byte for byte, the golden render through the VST3's audio
# path (tg_render_golden.h, the Move module's reference), and the window open
# under a running transport while a set loads on another thread. tg_host.cpp.
juce_add_console_app(tg_host PRODUCT_NAME tg_host)
target_sources(tg_host PRIVATE tg_host.cpp)
target_include_directories(tg_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(tg_host PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(tg_host PRIVATE
    tg_engine juce::juce_audio_processors juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME tg_host COMMAND tg_host ${NI_BUNDLES}/NITranceGate.vst3 ${NI_FIXTURES})

# EVERY OTHER PRODUCT: its processor, its audio callback and its built bundle,
# in a directory of its own (tests/<product>) with its own tiers -- found by
# the root build's product list, so a new product's tests join by existing.
# NI Trance Gate's are here, beside the JUCE shell's own.
foreach(product IN LISTS NI_PRODUCTS)
    if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${product}/CMakeLists.txt)
        add_subdirectory(${product})
    endif()
endforeach()
# THE SINGLE FILTERS, a family: their engine's, shell's and bundles' tests in
# one directory (tests/single-filter), each test every member's.
add_subdirectory(single-filter)

# ---------------------------------------------------------------- Spectrogram

# THE ANALYZER'S OWN TESTS, which are cargo's: the FFT against a naive DFT, the
# band mapping, and a counting global allocator proving the audio path allocates
# nothing. Run from here rather than left to be remembered.
#
# RUST_ENV and RUST_CARGO come from cmake/RustToolchain.cmake, which found the
# toolchain the hard way (rustup's shims are often not on PATH).
# -p, not --manifest-path: there is ONE workspace and it is at the repository
# root, so the crates are selected by name rather than by pointing cargo at a
# per-engine Cargo.toml that no longer exists.
ni_cargo_test(spectro_core spectro-core spectro-recv spectro-capi)

# The same claims through the C ABI the plugin links, because a hand-written
# header can drift from its implementation without either side failing to
# compile.
add_executable(spectro_columns spectro_columns.c)
target_link_libraries(spectro_columns PRIVATE spectro_engine)
if (NOT WIN32)
    target_link_libraries(spectro_columns PRIVATE m)
endif()
add_test(NAME spectro_columns COMMAND spectro_columns)

# The RECEIVER through the same door, and against a real bus rather than a
# stand-in. spectro-recv's cargo tests cover the behaviour; this covers the seam
# -- spectro_recv.h is generated from the Rust, and this is the C that calls
# through it, so a header that compiles but means something else still fails
# here.
#
# It links BOTH engines: the receiver is in libspectro_capi.a and the transport
# it reads is audio-bus, which is the one crate deliberately shared between two
# products.
add_executable(srecv_api srecv_api.c)
# ONE Rust staticlib per binary, and that is not a preference. libspectro_capi.a
# and libbus_capi.a each embed the Rust runtime, so a target linking both is a
# duplicate `rust_eh_personality` at link time. It does not arise in the
# plugins -- the Spectrogram links only spectro_engine, which already contains
# bus-core through spectro-recv -- and it is why the end-to-end test against a
# live sender is in Rust (crates/spectro-recv/tests/receiver.rs) rather than here.
target_link_libraries(srecv_api PRIVATE spectro_engine)
if (NOT WIN32)
    target_link_libraries(srecv_api PRIVATE m)
endif()
add_test(NAME srecv_api COMMAND srecv_api)


# ------------------------------------------------------------------ Listen-In
#
# The transport's own tests are cargo's: the ring, the claim protocol, the
# label seqlock, and a counting global allocator proving the audio path
# allocates nothing.
#
ni_cargo_test(abus_core bus-core bus-capi)

# The same claims through the C ABI the plugins link, because a hand-written
# header can drift from its implementation without either side failing to
# compile.
add_executable(abus_roundtrip abus_roundtrip.c)
target_link_libraries(abus_roundtrip PRIVATE audio_bus_engine)
add_test(NAME abus_roundtrip COMMAND abus_roundtrip)

# THE SHELL PLUMBING'S OWN TESTS: the bridge and the handoff, the property
# that holds the bridge to its round trip, the two-thread stress runs and the
# no-allocation check. Each product's bridge over it lives in that product's
# capi crate, and is tested with it (tg_core_rs, sc_core_rs: a capi crate's
# default features include its `shell`).
ni_cargo_test(shell_core shell-core shell-capi)

# THE SHARED CRATES' OWN TESTS. Every product's goldens cover them through the
# products; these pin each piece on its own -- the analyzer with its editor's
# half (the spectrum), which the workspace's features switch on (the shells'
# capi crates ask for it).
ni_cargo_test(ni_shared ni-dsp ni-schwung ni-params ni-filter ni-filter-node ni-analyzer)

# THE SCHWUNG HOST'S TABLES, FROM ITS OWN HEADERS. ni-schwung mirrors
# host_api_v1_t and audio_fx_api_v2_t in Rust; abi/layout.def writes their
# offsets, sizes and version numbers down once, ni_shared holds the mirrors to
# it and this holds the vendored C headers to it -- every entry a
# _Static_assert, so a disagreeing header does not build.
set(NI_SCHWUNG ${CMAKE_SOURCE_DIR}/engines/shared/crates/ni-schwung)
add_executable(ni_schwung_abi ${NI_SCHWUNG}/abi/layout.c)
target_include_directories(ni_schwung_abi PRIVATE ${TG_ENGINE}/include ${NI_SCHWUNG}/abi)
add_test(NAME ni_schwung_abi COMMAND ni_schwung_abi)
set_tests_properties(ni_schwung_abi PROPERTIES SKIP_RETURN_CODE 77)

# ...AND THOSE HEADERS ARE THE PINNED SCHWUNG'S, byte for byte: the version
# modules/_shared/schwung.version names, fetched once into a cache outside the
# checkout and verified on every run. 77, Skipped, when it is not cached and
# cannot be fetched; a moved tag or an edited cache fails.
add_test(NAME schwung_pin COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/schwung.sh headers)
set_tests_properties(schwung_pin PROPERTIES SKIP_RETURN_CODE 77)

# MUSIC-CORE, TWICE: with `std` and without it. The crate promises `no_std` and a
# method set that only ever loses the two frequency methods without `std`, and
# a build that is never made without it is a promise nobody checks. The README
# runs as doctests in both, so the front page cannot drift either.
ni_cargo_test(music_core_rs music-core)
add_test(NAME music_core_no_std
         COMMAND ${RUST_ENV} ${RUST_CARGO} test --manifest-path
                 ${CMAKE_SOURCE_DIR}/Cargo.toml -p music-core --no-default-features)

# THE PAPERS' CODE (papers/<name>/code). Every Rust listing a paper prints is
# a region of its crate, and the crate's snippet test fails when the two
# differ -- so a paper whose code stopped compiling, or drifted from its text,
# fails the quick tier like any engine would.
add_test(NAME paper_subsynth
         COMMAND ${RUST_ENV} ${RUST_CARGO} test --locked --manifest-path
                 ${CMAKE_SOURCE_DIR}/papers/subtractive-synthesis/code/Cargo.toml)

# ------------------------------------------------------------ NI Chord-Detector

# THE ENGINE'S OWN CLAIMS, which are cargo's: what is named for every kind of
# voicing, the pedal, Hold, the panic, the history's clock that never goes
# back, a counting allocator over everything the audio thread calls, and
# property tests over arbitrary MIDI.
# The C ABI from C: compiled against the header cbindgen writes
# (cd-capi/build.rs) and linked to the real archive.
add_executable(cd_core_tests ${CMAKE_SOURCE_DIR}/engines/chord-detector/tests/test_core.c)
target_link_libraries(cd_core_tests PRIVATE cd_engine)
if (NOT WIN32)
    target_link_libraries(cd_core_tests PRIVATE m)
endif()
add_test(NAME cd_core COMMAND cd_core_tests)

ni_cargo_test(cd_core_rs cd-core cd-capi)

# The handoff's seam, holding a real bus writer the way NI Listen-In does.
# The shell_handoff_* symbols ride in libbus_capi.a, so that is what it links.
add_executable(shell_handoff shell_handoff.c)
target_link_libraries(shell_handoff PRIVATE audio_bus_engine shell_engine)
add_test(NAME shell_handoff COMMAND shell_handoff)
set_tests_properties(shell_handoff PROPERTIES SKIP_RETURN_CODE 77)

# THE ONE THAT JUSTIFIES SHARED MEMORY.
#
# Every other test here would pass over a process-local ring. This one forks,
# writes in the child and reads in the parent -- which is the thing a static
# table inside the library could never do, and the reason the transport is
# shm_open rather than a `static BUSES[16]`. Without it, the day a host
# sandboxes a plugin the failure is a receiver that is simply always empty.
if (NOT WIN32)
    add_executable(abus_ipc abus_ipc.c)
    target_link_libraries(abus_ipc PRIVATE audio_bus_engine)
    add_test(NAME abus_ipc COMMAND abus_ipc)
    # It waits on a forked child through usleep loops; the default 1500s is
    # generous but a hang here should be a failure, not a stuck suite.
    set_tests_properties(abus_ipc PROPERTIES TIMEOUT 60)
endif()

# THE SAME, ACROSS THE TWO SLICES OF THE UNIVERSAL BINARY. Live under Rosetta
# is x86_64; a host beside it may be arm64; the bus is shared memory between
# them. abus_ipc --sender-arch execs its sender as the other slice, both ways
# round. Registered only where that can run at all: a universal build, on an
# arm64 Mac with Rosetta installed -- anywhere else there is no second slice
# to talk to, and a skip would only pretend.
if (APPLE AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64"
    AND "arm64" IN_LIST CMAKE_OSX_ARCHITECTURES
    AND "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
    execute_process(COMMAND /usr/bin/arch -x86_64 /usr/bin/true
                    RESULT_VARIABLE _ni_rosetta OUTPUT_QUIET ERROR_QUIET)
    if (_ni_rosetta EQUAL 0)
        add_test(NAME abus_ipc_rosetta
                 COMMAND abus_ipc --sender-arch x86_64)
        add_test(NAME abus_ipc_rosetta_reader
                 COMMAND /usr/bin/arch -x86_64 $<TARGET_FILE:abus_ipc>
                         --sender-arch arm64)
        set_tests_properties(abus_ipc_rosetta abus_ipc_rosetta_reader
                             PROPERTIES TIMEOUT 60)
    else()
        ni_missing_capability(rosetta "the cross-architecture bus tests (abus_ipc_rosetta*) are not registered")
    endif()
endif()

# NOT A TEST, AND DELIBERATELY BUILT ANYWAY: `abus_tap <slot>` is how a person
# confirms that Listen-In is routing real audio out of a real host, before any
# plugin exists that reads a bus. It is also the worked example of the
# receiving ABI. See plugins/listen-in/docs/live.md.
add_executable(abus_tap abus_tap.c)
target_link_libraries(abus_tap PRIVATE audio_bus_engine)
if (NOT WIN32)
    target_link_libraries(abus_tap PRIVATE m)
endif()

# THE COVERAGE FLOOR, registered only where there is a report to check.
#
# It reads build/coverage/coverage.json, which scripts/coverage.sh writes --
# running it from a plain build would fail on a missing file and teach nobody
# anything, so it appears in the instrumented build and there only.
if (TG_NODE AND VST_COVERAGE)
    add_test(NAME coverage_floor
             COMMAND ${TG_NODE} --test
                     ${CMAKE_SOURCE_DIR}/tests/coverage_floor.test.mjs)
    set_tests_properties(coverage_floor PROPERTIES
        ENVIRONMENT "COVERAGE_JSON=${CMAKE_SOURCE_DIR}/build/coverage/coverage.json")
endif()

# THE RUST LICENCE GATE. Every crate in the graph is under a licence on
# deny.toml's allowlist and comes from crates.io, and a crate that arrives in
# two versions is shown. No advisories: deny.toml says why they stay out of a
# quick tier.
#
# It needs cargo-deny at its pin (scripts/licence-tools.sh install cargo-deny),
# and without it the test FAILS, naming the missing subcommand: a licence
# check that skips is not one. Configure says so up front.
add_test(NAME cargo_deny
         COMMAND ${RUST_ENV} ${RUST_CARGO} deny --manifest-path ${CMAKE_SOURCE_DIR}/Cargo.toml
                 --config ${CMAKE_SOURCE_DIR}/deny.toml --locked
                 check licenses bans sources)
execute_process(COMMAND ${RUST_ENV} ${RUST_CARGO} deny --version
                RESULT_VARIABLE _ni_cargo_deny OUTPUT_QUIET ERROR_QUIET)
if (NOT _ni_cargo_deny EQUAL 0)
    message(WARNING "cargo-deny is not installed, so the cargo_deny test will fail. "
                    "Install it at its pin: scripts/licence-tools.sh install cargo-deny")
endif()

# THE ADVISORIES, ASKED SEPARATELY: RustSec's database, fetched when the test
# runs (deny.toml's [advisories]), so its verdict can change without a commit.
# That keeps it out of the quick tier and puts it in the full one and in each
# product's run -- what a release is attested by -- and both release workflows
# ask it again on the day they publish.
add_test(NAME cargo_deny_advisories
         COMMAND ${RUST_ENV} ${RUST_CARGO} deny --manifest-path ${CMAKE_SOURCE_DIR}/Cargo.toml
                 --config ${CMAKE_SOURCE_DIR}/deny.toml --locked
                 check advisories)

# THE TRANCE GATE'S RUST, which was the one suite nobody ran.
#
# spectro_core has been registered since the Spectrogram arrived, and the note
# says why -- "run from here rather than left to be remembered". The same argument applied to tg-core the whole time and the same
# two variables were already in scope; nothing was using them. A suite that only
# runs when somebody types `cargo test` in the right directory is, as run.sh
# puts it, the same as not being run.
ni_cargo_test(tg_core_rs tg-core tg-capi tg-move tg-web)

# ---------------------------------------------------------------- The C++
# The plain C++ every processor shares, with no JUCE and no engine:
# tests/cpp/CMakeLists.txt.
add_subdirectory(cpp)

# --------------------------------------------------------------- NI Side-Chain

# The engine's C tests, compiled against the header sc-capi's build.rs
# generates and linked to the real library. The header is the Rust's own
# declaration now, so it cannot drift from crates/sc-capi; these hold it to the
# C that calls it, which a generated header that compiles can still break.
set(SC_ENGINE ${CMAKE_SOURCE_DIR}/engines/side-chain)

add_executable(sc_core_tests ${SC_ENGINE}/tests/test_core.c)
target_link_libraries(sc_core_tests PRIVATE sc_engine)
if (NOT WIN32)
    target_link_libraries(sc_core_tests PRIVATE m)
endif()
add_test(NAME sc_core COMMAND sc_core_tests)

# THE DSP'S OWN CLAIMS, which are cargo's: the stage machine's bookkeeping, the
# detector's edge logic, the parameter clamps and the curve algebra. Stated
# where the state is reachable, rather than inferred through the ABI.
#
# sc-move is in here too, and its tests are the ones nothing else can make: that
# `chain_params` declares every key the engine answers to and no key it does
# not, that the viz group stays contiguous (the host DROPS a graphic whose
# members are not adjacent), and that the Sidechain source is absent where the
# hardware has no aux input to feed it.
ni_cargo_test(sc_core_rs sc-core sc-capi sc-move sc-web)

# THE CURVE ORACLE: the engine's duck shape against a table it generated, for
# the reason the Trance Gate's is (tg_curves). Regenerate with
# `sc_shape_table > <the file below>`, and ONLY when the curve maths is meant
# to change.
set(SC_FIXTURES ${SC_ENGINE}/tests/fixtures)

add_executable(sc_shape_table ${SC_ENGINE}/tests/shape_table.c)
target_link_libraries(sc_shape_table PRIVATE sc_engine)
if (NOT WIN32)
    target_link_libraries(sc_shape_table PRIVATE m)
endif()
add_test(NAME sc_shape COMMAND sc_shape_table --verify ${SC_FIXTURES}/shape_table.txt)

# THE ENVELOPE ORACLE, and it is MEASURED rather than transcribed.
#
# The curve table above pins shape(); this pins the whole stage machine around
# it -- the Delay, what the release falls from, what a zero-length stage does,
# and what the attack starts from when a previous release is still sounding.
#
# It drives the REAL engine with a DC input at depth 1, where the gain applied
# IS the envelope, so the reference owes nothing to anyone's reading of the
# Rust. --verify RE-RENDERS rather than re-reading, because measuring again is
# the whole point of a measured fixture.
add_executable(sc_envelope_table ${SC_ENGINE}/tests/envelope_table.c)
target_link_libraries(sc_envelope_table PRIVATE sc_engine)
if (NOT WIN32)
    target_link_libraries(sc_envelope_table PRIVATE m)
endif()
add_test(NAME sc_envelope COMMAND sc_envelope_table --verify ${SC_FIXTURES}/envelope_table.txt)

# THE TWO PATHS, RENDERED SIDE BY SIDE AND PINNED.
#
# AGENTS.md asks that every DSP exist on both the Live plugin and the Move
# module. This is that made checkable, and it makes two claims at once: the
# float/split path and the int16 path apply the SAME gain (within the one LSB
# int16 costs), and the float render still hashes to its recorded value.
#
# Neither is redundant. The A/B alone would be satisfied by an engine that broke
# identically in both directions; the hash alone would not notice a shell
# feeding one path a different transport.
add_executable(sc_render_ab ${SC_ENGINE}/tests/render_ab.c)
target_link_libraries(sc_render_ab PRIVATE sc_engine)
if (NOT WIN32)
    target_link_libraries(sc_render_ab PRIVATE m)
endif()
add_test(NAME sc_render_ab COMMAND sc_render_ab)

# --------------------------------------------------------------- NI Bit-Crusher

# THE ENGINE'S OWN CLAIMS, which are cargo's: the staircase and its
# antiderivative, the hold's clock, the filters, the glides and crossfades, the
# panic, the parameter table and its text, the aliasing Anti-Alias takes out,
# no allocation on any audio path -- and bc-move's `chain_params` held to the
# engine's table, through the vtable the device calls.
set(BC_ENGINE ${CMAKE_SOURCE_DIR}/engines/bit-crusher)
ni_cargo_test(bc_core_rs bc-core bc-capi bc-move)

# The C ABI from C: compiled against both headers cbindgen writes
# (bc-capi/build.rs) and linked to the plugin's real archive.
add_executable(bc_core_tests ${BC_ENGINE}/tests/test_core.c)
target_link_libraries(bc_core_tests PRIVATE bc_engine)
if (NOT WIN32)
    target_link_libraries(bc_core_tests PRIVATE m)
endif()
add_test(NAME bc_core COMMAND bc_core_tests)

# THE TWO PATHS, RENDERED SIDE BY SIDE AND PINNED, as the Side-Chain's are:
# the plugin's float path and the Move's int16 path within one LSB of each
# other, and the float render's hash recorded (render_golden.h).
add_executable(bc_render_ab ${BC_ENGINE}/tests/render_ab.c)
target_link_libraries(bc_render_ab PRIVATE bc_engine)
if (NOT WIN32)
    target_link_libraries(bc_render_ab PRIVATE m)
endif()
add_test(NAME bc_render_ab COMMAND bc_render_ab)

# ----------------------------------------------------------------- NI Vocalize

# THE ENGINE'S OWN CLAIMS, which are cargo's: the vowel model and its filter
# measured against its curve, linear prediction against a known process, the
# Spectral engine's reconstruction, the identity at Vocal 0, the vowel on
# white noise in both modes, the morph moving step by step, the switch's dip,
# the panic, the parameter table and its text, no allocation on any audio
# path. Three runs, because each is a different build of the same core:
#
#   vz_core_rs        the plugin's: the core with its Spectral engine, and the
#                     C ABI over it
#   vz_core_time_only the Move's core, without it: every Mode is Time
#   vz_move_rs        the module's crate ALONE, so Cargo builds it as it
#                     ships -- named beside the others it would be built with
#                     their features -- its `chain_params` held to the
#                     engine's table through the vtable the device calls
set(VZ_ENGINE ${CMAKE_SOURCE_DIR}/engines/vocalize)
ni_cargo_test(vz_core_rs vz-core vz-capi)
add_test(NAME vz_core_time_only
         COMMAND ${RUST_ENV} ${RUST_CARGO} test --manifest-path
                 ${CMAKE_SOURCE_DIR}/Cargo.toml -p vz-core --no-default-features)
add_test(NAME vz_move_rs
         COMMAND ${RUST_ENV} ${RUST_CARGO} test --manifest-path
                 ${CMAKE_SOURCE_DIR}/Cargo.toml -p vz-move)

# The C ABI from C: compiled against both headers cbindgen writes
# (vz-capi/build.rs) and linked to the plugin's real archive.
add_executable(vz_core_tests ${VZ_ENGINE}/tests/test_core.c)
target_link_libraries(vz_core_tests PRIVATE vz_engine)
if (NOT WIN32)
    target_link_libraries(vz_core_tests PRIVATE m)
endif()
add_test(NAME vz_core COMMAND vz_core_tests)

# THE TWO PATHS, RENDERED SIDE BY SIDE AND PINNED, as the Side-Chain's are:
# the plugin's float path and the Move's int16 path within one LSB of each
# other, a patch for each engine, and the float renders' hashes recorded
# (render_golden.h).
add_executable(vz_render_ab ${VZ_ENGINE}/tests/render_ab.c)
target_link_libraries(vz_render_ab PRIVATE vz_engine)
if (NOT WIN32)
    target_link_libraries(vz_render_ab PRIVATE m)
endif()
add_test(NAME vz_render_ab COMMAND vz_render_ab)

# ---------------------------------------------------------- NI Filter Designer

# THE ENGINE'S OWN CLAIMS, which are cargo's: every node's sound against its
# design, the same bits at any block size, an empty instance bit-exact, no
# click when what a node is changes, the isolator flat at 0 dB and its kills
# deep, mid and side kept apart, the panic, the parameter table and its text,
# the picture against the design and the analyzer against a sine, no
# allocation on any audio path. (ni-filter's, ni-filter-node's and
# ni-analyzer's own tests are ni_shared's.)
set(FD_ENGINE ${CMAKE_SOURCE_DIR}/engines/filter-designer)
ni_cargo_test(fd_core_rs fd-core fd-capi)

# The C ABI from C: compiled against both headers cbindgen writes
# (fd-capi/build.rs) and linked to the plugin's real archive.
add_executable(fd_core_tests ${FD_ENGINE}/tests/test_core.c)
target_link_libraries(fd_core_tests PRIVATE fd_engine)
if (NOT WIN32)
    target_link_libraries(fd_core_tests PRIVATE m)
endif()
add_test(NAME fd_core COMMAND fd_core_tests)
# The Move module's crate ALONE, so Cargo builds it as it ships, its
# chain_params held to the engine's table through the vtable the device calls.
add_test(NAME fd_move_rs
         COMMAND ${RUST_ENV} ${RUST_CARGO} test --manifest-path
                 ${CMAKE_SOURCE_DIR}/Cargo.toml -p fd-move)
# THE SOAK, in release: ten minutes of noise with NaN, ±Inf and denormals
# through every node and the isolator, bit for bit the clean input's sound,
# then rung down to exactly nothing (fd-core/tests/soak.rs).
add_test(NAME fd_soak_rs
         COMMAND ${RUST_ENV} ${RUST_CARGO} test --release --manifest-path
                 ${CMAKE_SOURCE_DIR}/Cargo.toml -p fd-core --test soak -- --ignored)

# THE TWO PATHS, RENDERED SIDE BY SIDE AND PINNED: the plugin's float path and
# the Move's int16 path within one LSB, and the float render's hash recorded
# (render_golden.h).
add_executable(fd_render_ab ${FD_ENGINE}/tests/render_ab.c)
target_link_libraries(fd_render_ab PRIVATE fd_engine)
if (NOT WIN32)
    target_link_libraries(fd_render_ab PRIVATE m)
endif()
add_test(NAME fd_render_ab COMMAND fd_render_ab)

# EACH CARGO SUITE ON BUSES OF ITS OWN. The workspace's .cargo/config.toml
# gives every `cargo test` the checkout's bus namespace, which keeps a suite
# off a Live session's buses -- but ctest runs these suites side by side, and
# two of them in one namespace claim and unlink each other's slots (abus_core
# failed "slot 8 could not be claimed" so). Set here, the variable wins over
# the config's, which never forces it.
foreach(t tg_core_rs spectro_core abus_core shell_core ni_shared sc_core_rs)
    set_property(TEST ${t} APPEND PROPERTY ENVIRONMENT "NIA_BUS_NS=${CMAKE_BINARY_DIR}/${t}")
endforeach()

# ============================================================ THE MOVE MODULES
#
# Every module under modules/ (one with a module.env), built and tested as the
# Move runs it: modules/_shared/test.sh packages the tarball in the module
# image, checks what it carries -- an aarch64 .so with nothing of the plugin's
# shell in it, the licence and the notices of every crate it links --
# and runs the module's crates and test script on aarch64 Linux, the Trance
# Gate's golden render among them. A new module is tested here by having a
# module.env.
#
# Docker is the toolchain, so without it these are not registered (as the
# Rosetta tests are not without Rosetta), and the configure says so. One at a
# time: both build into target/aarch64-unknown-linux-gnu/ from one container
# each, and two cargos in one target directory across two containers is not a
# lock worth relying on.
set(_ni_move_tiers)
file(GLOB _ni_module_envs ${CMAKE_SOURCE_DIR}/modules/*/module.env)
foreach(env IN LISTS _ni_module_envs)
    get_filename_component(_dir ${env} DIRECTORY)
    get_filename_component(_module ${_dir} NAME)
    list(APPEND _ni_move_tiers PRODUCT ${_module} FULL move_${_module}:move)
    if (SCHWUNG_DOCKER)
        add_test(NAME move_${_module}
                 COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/test.sh ${_module})
        set_tests_properties(move_${_module} PROPERTIES RESOURCE_LOCK ni_move_target)
    endif()
endforeach()
if (NOT SCHWUNG_DOCKER)
    ni_missing_capability(docker "the Move modules' tests (move_*) are not registered or built")
endif()

# ====================================================== THE TIERS, BY PRODUCT
#
# cmake/NiTest.cmake says what the tiers and the products mean, and checks that
# this list is exhaustive: every test here in a tier and a product, every
# program in the product whose tests run it. tests/ui, tests/cpp and
# tests/<product> place their own.
ni_test_tiers(
    PRODUCT shared
        PROGRAMS juce_host nist_fixtures ni_alloc_guard abus_roundtrip abus_ipc abus_tap
                 shell_handoff ni_schwung_abi
        QUICK
            # Rust: the shared crates' unit tests, and the licence gate over
            # the whole graph
            abus_core shell_core ni_shared music_core_rs music_core_no_std
            # every suite's build (cmake/NiCargoTest.cmake), and the doctests
            rust_tests_build rust_doctests
            cargo_deny
            # the Schwung host's tables: the vendored headers against the
            # layout the Rust mirrors are held to, and against the pinned host
            ni_schwung_abi schwung_pin schwung_versions
            # C against the shared ABIs: the bus and the handoff
            abus_roundtrip shell_handoff
            # lint-like checks over the tree
            versions release licenses licenses_resolve spdx site_tokens
            design_paths doc_paths product_matrix timing toolchain time_macros test_summary
            # the Move web editors' kit
            web_kit
            # the JUCE shell's state codec, over every product's fixtures
            nist_fixtures
            # the papers' code: every listing a paper prints, compiled and tested
            paper_subsynth
        FULL
            abus_ipc:ipc abus_ipc_rosetta:ipc abus_ipc_rosetta_reader:ipc
            site_links:site coverage_floor:coverage
            cargo_deny_advisories:advisories
    PRODUCT trance-gate
        PROGRAMS tg_core_tests tg_render_plugin tg_shape_table tg_envelope_table tg_fade_table
                 tg_processor tg_rt tg_host
        QUICK
            tg_core_rs tg_core tg_curves tg_envelope tg_fade
            # its processor, model and editor in one program, and its audio
            # callback under the allocation guard
            tg_processor tg_rt
            # the Move module's editor, run against Schwung's shared modules
            tg_move_ui
            # its web editor on the Move
            web_tg
        FULL tg_render_ab:render tg_host:host web_tg_e2e:browser
    PRODUCT spectrogram
        PROGRAMS spectro_columns srecv_api
        QUICK spectro_core spectro_columns srecv_api
    PRODUCT side-chain
        PROGRAMS sc_core_tests sc_shape_table sc_envelope_table sc_render_ab
        QUICK sc_core_rs sc_core sc_shape sc_envelope web_sc
        FULL sc_render_ab:render web_sc_e2e:browser
    PRODUCT chord-detector
        PROGRAMS cd_core_tests
        QUICK cd_core_rs cd_core
    PRODUCT bit-crusher
        PROGRAMS bc_core_tests bc_render_ab
        QUICK bc_core_rs bc_core
        FULL bc_render_ab:render
    PRODUCT vocalize
        PROGRAMS vz_core_tests vz_render_ab
        QUICK vz_core_rs vz_core_time_only vz_move_rs vz_core
        FULL vz_render_ab:render
    PRODUCT filter-designer
        PROGRAMS fd_core_tests fd_render_ab
        QUICK fd_core_rs fd_move_rs fd_core
        FULL fd_render_ab:render fd_soak_rs:soak
    # every bundle's host, signature and tree checks, and every Move module
    ${_ni_juce_tiers}
    ${_ni_move_tiers})

# What builds them: ni_tests, and ni_tests_<product> for one product's run.
ni_test_targets()

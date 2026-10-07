# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# THE JUCE SHELL'S BUILD: one call per product.
#
#   ni_add_juce_plugin(<product>
#       TARGET <bundle name>        NIChordDetector -> NIChordDetector.vst3
#       NAME "<plugin name>"        "NI Chord-Detector": what a host lists
#       CODE <four characters>      the plugin code, beside the maker's Grbe
#       ENGINE <target>             the product's Rust engine (one archive)
#       [CATEGORIES <VST3 ...>]     Fx by default
#       [SYNTH] [MIDI_IN]           an instrument, and/or MIDI into it
#       [VST3_CLASS <32 hex>]       keep an older build's class ID (below)
#       [IPLUG2_CLASS]              the class the iPlug2 bundle TARGET had, from
#                                   tests/fixtures/iplug2/ids.json
#       [LEGACY_PARAM_IDS]          a parameter's index is its VST3 ID
#       [EDITOR]                    plugins/<product>/editor, built on the kit
#       [NOTICES <files>]           more licence texts the bundle carries
#       SOURCES <files>)            the processor and what it needs
#
# WHAT IT DECIDES ONCE, FOR EVERY PRODUCT:
#
#   VST3 only, from Neon Ingvy (maker code Grbe), with JUCE's web browser and
#   curl compiled out: no product opens a page or a connection. No splash
#   screen either, and no switch for one: JUCE 9 has none, and defining
#   JUCE_DISPLAY_SPLASH_SCREEN only earns its "the flag is ignored" warning.
#
#   THE VERSION IS versions.json's. "v2026.10.07.1" is v<date>.<n>, and JUCE
#   takes three numbers, so the date is the VERSION (2026.10.7: what the
#   Finder shows, CFBundleShortVersionString), the build number folds the
#   subversion into the day as tests/versions.test.mjs derives it
#   (2026.10.57 = day*8+sub: CFBundleVersion, which tells two builds of one
#   day apart), and the whole string is NI_VERSION_STRING for the editor.
#
#   THE NOTICES TRAVEL INSIDE THE BUNDLE: LICENSE and THIRD_PARTY_LICENSES.md
#   (NI_BUNDLE_NOTICES, the iPlug2 bundles' list), the AGPLv3 and Apache 2.0
#   texts of JUCE and what it compiles in (licenses/), the kit's font licence
#   (OFL.txt) for a product with an EDITOR -- the kit embeds the font -- and
#   the NOTICES given.
#
#   SIGNED LAST. JUCE signs its bundle and then writes
#   Contents/Resources/moduleinfo.json into it, which breaks the seal: Live's
#   scanner rejects such a bundle ("a sealed resource is missing or invalid")
#   while pluginval loads it happily. So the last of the bundle's POST_BUILD
#   steps -- after JUCE's, which run in the order they were added -- copies
#   the notices in and signs ad hoc, and a full-tier test runs
#   `codesign --verify --deep --strict` on what was built (NI_JUCE_BUNDLES).
#
#   CLASS IDS. VST3_CLASS is the class an older build's factory reported (its
#   FUID, as tests/fixtures/iplug2/ids.json writes it). JUCE takes a class by
#   its TUID bytes, which Windows stores in GUID order -- the same class,
#   other bytes -- so the call reorders it there. With it a saved set finds
#   this build exactly as it found the old one (JUCE_VST3_COMPONENT_CLASS:
#   JUCE's own switch, and the one Live honours). Without it JUCE derives the
#   class from the maker and plugin codes, which is right for a new product.
#   IPLUG2_CLASS takes the class from the fixtures the iPlug2 factories were
#   asked for (ids.json), and checks the bytes this platform gets against the
#   ones recorded there, so a product taking over an iPlug2 bundle cannot be
#   given a class that differs from it by a typo or a byte order.
#
#   OUT AND DEPLOYED as the iPlug2 bundles are: copied to build/out, and to
#   ~/Library/Audio/Plug-Ins/VST3 when IPLUG_DEPLOY_PLUGINS is on (the root
#   build's switch: -DIPLUG_DEPLOY_PLUGINS=OFF keeps them in build/out).
#
# The C++ shell the processor derives from is plugins/_shared/juce
# (ni::Processor, ni::PluginEditor): every product's sources get it.

set(NI_JUCE_SHELL_DIR ${CMAKE_SOURCE_DIR}/plugins/_shared/juce)
file(GLOB NI_JUCE_SHELL_SOURCES CONFIGURE_DEPENDS ${NI_JUCE_SHELL_DIR}/*.cpp)
set(NI_JUCE_OUT ${CMAKE_BINARY_DIR}/out)

# What a VST3 module exports: its entry points and nothing else. Everything
# else the binary holds -- JUCE, its bundled C libraries, the Rust engine's
# C ABI -- stays inside it, so two plugins in one host never bind to each
# other's copies (ELF interposes global symbols across modules; macOS keeps
# them apart by two-level namespace, but has no reason to see them).
if (APPLE)
    set(NI_JUCE_EXPORTS ${CMAKE_BINARY_DIR}/ni_vst3_exports.txt)
    file(WRITE ${NI_JUCE_EXPORTS} "_GetPluginFactory\n_bundleEntry\n_bundleExit\n")
elseif (UNIX)
    set(NI_JUCE_EXPORTS ${CMAKE_BINARY_DIR}/ni_vst3_exports.map)
    file(WRITE ${NI_JUCE_EXPORTS}
        "{\n  global: GetPluginFactory; ModuleEntry; ModuleExit;\n  local: *;\n};\n")
endif()

if (NOT DEFINED IPLUG_DEPLOY_PLUGINS)
    option(IPLUG_DEPLOY_PLUGINS "Deploy built plugins to system directories" ON)
endif()
if (APPLE)
    set(NI_JUCE_DEPLOY_VST3 "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
elseif (WIN32)
    set(NI_JUCE_DEPLOY_VST3 "$ENV{CommonProgramFiles}/VST3")
else()
    set(NI_JUCE_DEPLOY_VST3 "$ENV{HOME}/.vst3")
endif()

# The version versions.json gives a product, as { "v2026.10.07.1", 2026.10.7,
# 2026.10.57 }.
function(ni_juce_version product out_string out_numbers out_build)
    file(READ ${CMAKE_SOURCE_DIR}/versions.json versions)
    string(JSON v ERROR_VARIABLE err GET "${versions}" "${product}")
    if (err)
        message(FATAL_ERROR "versions.json has no version for '${product}'")
    endif()
    if (NOT v MATCHES "^v([0-9][0-9][0-9][0-9])\\.([0-9][0-9])\\.([0-9][0-9])\\.([0-9]+)$")
        message(FATAL_ERROR "${product}: '${v}' is not v<YYYY.MM.DD>.<n>")
    endif()
    math(EXPR month "${CMAKE_MATCH_2}")
    math(EXPR day "${CMAKE_MATCH_3}")
    math(EXPR packed_day "${day} * 8 + ${CMAKE_MATCH_4}")
    set(${out_string} "${v}" PARENT_SCOPE)
    set(${out_numbers} "${CMAKE_MATCH_1}.${month}.${day}" PARENT_SCOPE)
    set(${out_build} "${CMAKE_MATCH_1}.${month}.${packed_day}" PARENT_SCOPE)
endfunction()

# A class ID as the factory reports it (FUID order) to the bytes JUCE wants
# on this platform.
function(ni_juce_class_bytes fuid out)
    string(TOUPPER "${fuid}" fuid)
    if (NOT fuid MATCHES "^[0-9A-F]+$")
        message(FATAL_ERROR "VST3_CLASS '${fuid}' is not hexadecimal")
    endif()
    string(LENGTH "${fuid}" n)
    if (NOT n EQUAL 32)
        message(FATAL_ERROR "VST3_CLASS '${fuid}' is not 32 hexadecimal digits")
    endif()
    if (WIN32)
        # GUID order: the first 32-bit word and the two 16-bit words reversed.
        string(SUBSTRING "${fuid}" 0 8 a)
        string(SUBSTRING "${fuid}" 8 4 b)
        string(SUBSTRING "${fuid}" 12 4 c)
        string(SUBSTRING "${fuid}" 16 16 rest)
        set(swapped "")
        foreach(word IN ITEMS a b c)
            string(LENGTH "${${word}}" len)
            set(r "")
            math(EXPR last "${len} - 2")
            foreach(i RANGE 0 ${last} 2)
                string(SUBSTRING "${${word}}" ${i} 2 byte)
                set(r "${byte}${r}")
            endforeach()
            string(APPEND swapped "${r}")
        endforeach()
        set(fuid "${swapped}${rest}")
    endif()
    set(${out} "${fuid}" PARENT_SCOPE)
endfunction()

# The class an iPlug2 bundle's factory reported, from the fixtures, and the
# bytes it is on this platform -- checked against those recorded there.
function(ni_juce_iplug2_class bundle out)
    file(READ ${CMAKE_SOURCE_DIR}/tests/fixtures/iplug2/ids.json ids)
    string(JSON cid ERROR_VARIABLE err GET "${ids}" "${bundle}" cid)
    if (err)
        message(FATAL_ERROR "tests/fixtures/iplug2/ids.json has no class for '${bundle}'")
    endif()
    if (WIN32)
        string(JSON want GET "${ids}" "${bundle}" tuidBytesWindows)
    else()
        string(JSON want GET "${ids}" "${bundle}" tuidBytesMacOS)
    endif()
    ni_juce_class_bytes(${cid} bytes)
    if (NOT bytes STREQUAL want)
        message(FATAL_ERROR "${bundle}: class ${cid} is ${bytes} here, but ids.json records ${want}")
    endif()
    set(${out} "${cid}" PARENT_SCOPE)
endfunction()

function(ni_add_juce_plugin product)
    cmake_parse_arguments(ARG "SYNTH;MIDI_IN;LEGACY_PARAM_IDS;EDITOR;IPLUG2_CLASS"
        "TARGET;NAME;CODE;ENGINE;VST3_CLASS" "CATEGORIES;NOTICES;SOURCES" ${ARGN})
    foreach(required TARGET NAME CODE ENGINE SOURCES)
        if (NOT ARG_${required})
            message(FATAL_ERROR "ni_add_juce_plugin(${product}) needs ${required}")
        endif()
    endforeach()
    if (NOT ARG_CATEGORIES)
        set(ARG_CATEGORIES Fx)
    endif()
    if (ARG_IPLUG2_CLASS)
        if (ARG_VST3_CLASS)
            message(FATAL_ERROR "ni_add_juce_plugin(${product}): VST3_CLASS or IPLUG2_CLASS, not both")
        endif()
        ni_juce_iplug2_class(${ARG_TARGET} ARG_VST3_CLASS)
    endif()

    ni_juce_version(${product} version_string version_numbers version_build)
    set(synth FALSE)
    set(midi_in FALSE)
    if (ARG_SYNTH)
        set(synth TRUE)
    endif()
    if (ARG_MIDI_IN)
        set(midi_in TRUE)
    endif()

    juce_add_plugin(${ARG_TARGET}
        PRODUCT_NAME "${ARG_TARGET}"
        PLUGIN_NAME "${ARG_NAME}"
        DESCRIPTION "${ARG_NAME}"
        VERSION ${version_numbers}
        BUILD_VERSION ${version_build}
        COMPANY_NAME "Neon Ingvy"
        COMPANY_COPYRIGHT "Copyright (C) 2026 Torben Gräber. GPL-3.0-or-later."
        COMPANY_WEBSITE "https://github.com/graebe/neon-ingvy-audio-plugins"
        BUNDLE_ID "com.graebe.vst3.${ARG_TARGET}"
        PLUGIN_MANUFACTURER_CODE Grbe
        PLUGIN_CODE ${ARG_CODE}
        FORMATS VST3
        VST3_CATEGORIES ${ARG_CATEGORIES}
        IS_SYNTH ${synth}
        NEEDS_MIDI_INPUT ${midi_in}
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    # NOTHING BUT THE ENTRY POINTS LEAVES THE BUNDLE. With default
    # visibility a plugin exports every inline function of JUCE as a weak
    # symbol, and so does a JUCE host; the macOS loader coalesces weak
    # definitions across images, so the plugin ends up calling the HOST's copy
    # of an inline juce::String function and frees what its own allocator
    # never gave out ("pointer being freed was not allocated", the debug
    # juce_host under coverage). Release builds hid it, link-time optimisation
    # internalising the symbols; hidden visibility makes it true for every
    # configuration and every host.
    foreach(t ${ARG_TARGET} ${ARG_TARGET}_VST3)
        set_target_properties(${t} PROPERTIES
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            OBJCXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON)
    endforeach()

    if (APPLE)
        target_link_options(${ARG_TARGET}_VST3 PRIVATE "LINKER:-exported_symbols_list,${NI_JUCE_EXPORTS}")
    elseif (UNIX)
        target_link_options(${ARG_TARGET}_VST3 PRIVATE "LINKER:--version-script=${NI_JUCE_EXPORTS}")
    endif()
    # Windows exports what JUCE marks dllexport: the entry points alone.

    target_sources(${ARG_TARGET} PRIVATE ${ARG_SOURCES} ${NI_JUCE_SHELL_SOURCES})
    target_include_directories(${ARG_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${NI_JUCE_SHELL_DIR})
    target_compile_definitions(${ARG_TARGET} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        # No WebP decoder: no product loads one, and juce_graphics would
        # otherwise compile libwebp in. PNG and JPEG it compiles in regardless
        # (THIRD_PARTY_LICENSES.md lists what a bundle carries).
        JUCE_USE_WEBP=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        "NI_VERSION_STRING=\"${version_string}\""
        "NI_PRODUCT=\"${product}\"")
    if (ARG_LEGACY_PARAM_IDS)
        target_compile_definitions(${ARG_TARGET} PUBLIC JUCE_FORCE_USE_LEGACY_PARAM_IDS=1)
    endif()
    if (ARG_VST3_CLASS)
        ni_juce_class_bytes(${ARG_VST3_CLASS} class_bytes)
        target_compile_definitions(${ARG_TARGET} PUBLIC "JUCE_VST3_COMPONENT_CLASS=\"${class_bytes}\"")
    endif()

    # The plugin client and the processors, and none of juce_audio_utils:
    # no product reads or writes an audio file or opens a device, and that
    # module would compile juce_audio_formats' codecs (FLAC, Ogg Vorbis, Opus,
    # an MP3 decoder) into every bundle. A product that needs it links it.
    set(link ${ARG_ENGINE} juce::juce_audio_processors)
    if (ARG_EDITOR)
        string(REPLACE "-" "_" id ${product})
        if (NOT TARGET ni_editor_${id})
            ni_ui_add_editor(${product})
        endif()
        list(APPEND link ni_editor_${id})
    endif()
    target_link_libraries(${ARG_TARGET}
        PRIVATE ${link}
        PUBLIC  juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)

    # The bundle: notices in, signed last, then out and deployed.
    get_target_property(bundle ${ARG_TARGET}_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
    # JUCE's AGPLv3 and the Apache 2.0 of a library it compiles in travel
    # with every bundle (THIRD_PARTY_LICENSES.md, JUCE's own dependencies).
    set(notices ${NI_BUNDLE_NOTICES}
        ${CMAKE_SOURCE_DIR}/licenses/AGPL-3.0.txt
        ${CMAKE_SOURCE_DIR}/licenses/Apache-2.0.txt
        ${ARG_NOTICES})
    if (ARG_EDITOR)
        list(APPEND notices ${NI_UI_FONT_LICENSE})
    endif()
    # The steps below run when the bundle is linked, and only then: a notice
    # edited later would otherwise stay stale in every bundle an incremental
    # build leaves alone. As a link dependency, editing one relinks the bundle,
    # and so copies it in and signs again.
    set_property(TARGET ${ARG_TARGET}_VST3 APPEND PROPERTY LINK_DEPENDS ${notices})
    set(steps
        COMMAND ${CMAKE_COMMAND} -E make_directory "${bundle}/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy ${notices} "${bundle}/Contents/Resources/")
    if (APPLE)
        list(APPEND steps
            COMMAND codesign --force --sign - --timestamp=none "${bundle}")
    endif()
    list(APPEND steps
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${NI_JUCE_OUT}/${ARG_TARGET}.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${bundle}" "${NI_JUCE_OUT}/${ARG_TARGET}.vst3")
    if (IPLUG_DEPLOY_PLUGINS)
        list(APPEND steps
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${NI_JUCE_DEPLOY_VST3}/${ARG_TARGET}.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${bundle}" "${NI_JUCE_DEPLOY_VST3}/${ARG_TARGET}.vst3")
    endif()
    add_custom_command(TARGET ${ARG_TARGET}_VST3 POST_BUILD ${steps}
        COMMENT "${ARG_TARGET}.vst3: notices, signature, build/out"
        VERBATIM)

    # What the tests check: every JUCE bundle, by its place in build/out, and
    # the name a host should list it by.
    set_property(GLOBAL APPEND PROPERTY NI_JUCE_BUNDLES "${ARG_TARGET}")
    set_property(GLOBAL PROPERTY NI_JUCE_NAME_${ARG_TARGET} "${ARG_NAME}")
endfunction()

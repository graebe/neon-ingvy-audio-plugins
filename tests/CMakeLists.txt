# The engine's own tests, built against the SUBMODULE -- the same library the
# plugin links. If these pass here and fail on Move, the submodule pin moved.
#
# tg_engine is the submodule's Rust crate, built once at the top level; see
# cmake/TgEngine.cmake.
#
# THREE TARGETS, DOWN FROM SIX. tg_params, tg_vst3 and tg_shot hosted JUCE
# classes directly -- they instantiated the AudioProcessor, hosted the VST3
# through JUCE's plugin host, and rendered the editor -- so they could not
# outlive it. What remains is everything that was written in plain C against
# the engine's ABI, which is also everything that checks the SOUND.
set(TG_ENGINE ${CMAKE_SOURCE_DIR}/external/schwung-trance-gate)

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

# The installed AU, rendered by a host that actually supplies a transport.
# AVAudioEngine supplies none, so a plugin under it correctly holds the gate
# open and renders dry -- a green render that proves nothing. This one sets
# the host callbacks the AU wrapper reads, drives the macros through the AU
# parameter API, and compares against the engine directly.
#
# It finds the plugin by the (aufx, TrGt, Grbe) triple, which the iPlug2 build
# carries over from the JUCE one -- so this test now covers the new AU without
# a line changing in it.
if (APPLE)
    add_executable(tg_au_render au_render.c)
    target_link_libraries(tg_au_render PRIVATE tg_engine m
        "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreFoundation")
    add_test(NAME tg_au COMMAND tg_au_render)
endif()

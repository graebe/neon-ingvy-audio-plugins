# The engine's own tests, built against the SUBMODULE -- the same sources the
# plugin links. If these pass here and fail on Move, the submodule pin moved.
set(TG_ENGINE ${CMAKE_SOURCE_DIR}/external/schwung-trance-gate)

add_executable(tg_core_tests ${TG_ENGINE}/tests/test_core.c
                             ${TG_ENGINE}/src/dsp/trance_gate_core.c)
target_include_directories(tg_core_tests PRIVATE ${TG_ENGINE}/src/dsp)
if (NOT WIN32)
    target_link_libraries(tg_core_tests PRIVATE m)
endif()
add_test(NAME tg_core COMMAND tg_core_tests)

# Renders through the PLUGIN's audio path (float, split channels, a DAW-shaped
# transport) and compares against the Move module's reference render. This is
# the check that the port did not change the sound.
add_executable(tg_render_plugin render_plugin.c ${TG_ENGINE}/src/dsp/trance_gate_core.c)
target_include_directories(tg_render_plugin PRIVATE ${TG_ENGINE}/src/dsp)
if (NOT WIN32)
    target_link_libraries(tg_render_plugin PRIVATE m)
endif()
# --verify hashes the render in-process and compares against the Move
# module's own reference, so the A/B runs with ctest instead of being a pipe
# into md5 that somebody has to remember. This is the check that the PORT did
# not change the sound; the engine repo's suite checks the engine itself.
add_test(NAME tg_render_ab COMMAND tg_render_plugin --verify)

# The parameter layer. Links the plugin's own shared-code target, so this is
# the same TranceGateProcessor the VST3 and the AU instantiate -- not a copy
# of it compiled with different flags.
juce_add_console_app(tg_param_tests PRODUCT_NAME "tg_param_tests")
target_sources(tg_param_tests PRIVATE test_params.cpp)
target_link_libraries(tg_param_tests PRIVATE TranceGate juce::juce_audio_utils)
target_include_directories(tg_param_tests PRIVATE ${TG_ENGINE}/src/dsp)
# The slot refresh is deliberately asynchronous (it touches the host's
# automation lanes, which belong to the message thread), so the test has to be
# able to PUMP that thread to see the result. Console-app only -- a plugin
# never runs a modal loop.
target_compile_definitions(tg_param_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
add_test(NAME tg_params COMMAND tg_param_tests)

# The installed AU, rendered by a host that actually supplies a transport.
# AVAudioEngine supplies none, so a plugin under it correctly holds the gate
# open and renders dry -- a green render that proves nothing. This one sets
# the host callbacks JUCE's AU wrapper reads, drives the macros through the AU
# parameter API, and compares against the engine directly.
if (APPLE)
    add_executable(tg_au_render au_render.c ${TG_ENGINE}/src/dsp/trance_gate_core.c)
    target_include_directories(tg_au_render PRIVATE ${TG_ENGINE}/src/dsp)
    target_link_libraries(tg_au_render PRIVATE m
        "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreFoundation")
    add_test(NAME tg_au COMMAND tg_au_render)
endif()

# The VST3, hosted. This is the format Ableton Live loads, and its parameter
# path is NOT the AU's -- VST3 carries a step count where AU converts against
# a published maximum, so a discrete/continuous mistake surfaces differently
# in each. JUCE bundles the VST3 interfaces it needs to host.
juce_add_console_app(tg_vst3_tests PRODUCT_NAME "tg_vst3_tests")
target_sources(tg_vst3_tests PRIVATE test_vst3.cpp ${TG_ENGINE}/src/dsp/trance_gate_core.c)
target_compile_definitions(tg_vst3_tests PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(tg_vst3_tests PRIVATE juce::juce_audio_utils)
target_include_directories(tg_vst3_tests PRIVATE ${TG_ENGINE}/src/dsp)
add_test(NAME tg_vst3 COMMAND tg_vst3_tests)

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

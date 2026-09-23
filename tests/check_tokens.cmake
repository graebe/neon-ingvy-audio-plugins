# THE DESIGN SYSTEM DRIFTS ONE HARD-CODED GREY AT A TIME.
#
# Every colour in the plugin must come from Phosphor.h, which is the file a
# re-sync of the design system diffs against. A literal typed anywhere else
# still compiles, still looks approximately right, and is invisible until
# somebody compares the window to the system and cannot say why it is off.
#
# So: no hex colour literals and no juce::Colours:: outside Phosphor.h. The
# one exception is transparentBlack, which is not a colour choice -- it is how
# JUCE spells "do not paint this".

file(GLOB sources "${SRC}/*.cpp" "${SRC}/*.h")
set(bad "")

foreach(file ${sources})
    get_filename_component(name "${file}" NAME)
    if(name STREQUAL "Phosphor.h")
        continue()
    endif()

    file(STRINGS "${file}" lines)
    set(n 0)
    foreach(line ${lines})
        math(EXPR n "${n}+1")
        # A comment is prose, not code: the tokens are quoted in several.
        string(REGEX REPLACE "^[ \t]*[*/].*" "" code "${line}")
        if(code MATCHES "0x[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]")
            list(APPEND bad "${name}:${n}: hex colour literal -- ${code}")
        endif()
        if(code MATCHES "Colours::" AND NOT code MATCHES "Colours::transparentBlack")
            list(APPEND bad "${name}:${n}: juce::Colours -- ${code}")
        endif()
    endforeach()
endforeach()

if(bad)
    message("  colours found outside Phosphor.h:")
    foreach(b ${bad})
        message("    ${b}")
    endforeach()
    message(FATAL_ERROR "every colour belongs in Phosphor.h -- see the note at the top of tests/check_tokens.cmake")
endif()

message("  every colour comes from Phosphor.h                       ok")

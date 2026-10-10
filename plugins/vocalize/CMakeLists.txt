# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Vocalize: any sound, more and more a voice singing A, E, I, O or U. The
# engine is engines/vocalize (vz_engine); the editor is editor/, built on the
# kit. No MIDI in: on the Move the panic arrives as MIDI, in Live as a
# reactivation (Vocalize.h).

# THE RECORDINGS A VOICE IS CAPTURED FROM: juce_audio_formats, which no other
# product links (cmake/NiJucePlugin.cmake), with its third-party codecs
# switched off -- FLAC, Ogg Vorbis, Opus, MP3 -- so what is compiled in is
# JUCE's own WAV and AIFF readers and, on macOS, the system's decoders
# (CoreAudioFormat: m4a, mp3, caf), and nothing new to license. The plugin and
# the tests that compile its processor link this.
add_library(vz_recordings INTERFACE)
target_link_libraries(vz_recordings INTERFACE juce::juce_audio_formats)
target_compile_definitions(vz_recordings INTERFACE
    JUCE_USE_FLAC=0 JUCE_USE_OGGVORBIS=0 JUCE_USE_OPUS=0 JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_USE_LAME_AUDIO_FORMAT=0 JUCE_USE_WINDOWS_MEDIA_FORMAT=0)
ni_add_juce_plugin(vocalize
    TARGET NIVocalize
    NAME "NI Vocalize"
    CODE Vclz
    ENGINE vz_engine
    CATEGORIES Fx Filter
    EDITOR
    SOURCES Vocalize.cpp Vocalize.h)
target_link_libraries(NIVocalize PRIVATE vz_recordings)

/*
 * Spectrogram -- iPlug2 build configuration.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * The second plugin in this repository, and it exists to be looked at rather
 * than listened to: audio passes through untouched and the editor draws a
 * rolling time/frequency picture of it in the Ultraviolet design language.
 *
 * iPlug2 (zlib) + the VST3 SDK (MIT) + CLAP (MIT) + an analyzer crate with no
 * dependencies at all. Nothing here is copyleft, which is the same reason the
 * Trance Gate is on iPlug2 rather than JUCE.
 */
#pragma once

#define PLUG_NAME "Spectrogram"
#define PLUG_MFR "graebe"
/*
 * 0.1.0, AND IT USED TO SAY 1.0.0 HERE WHILE THE CRATES SAID 0.1.0.
 *
 * The plugin and the analyzer inside it disagreed about what they were, which
 * is the kind of thing nobody notices until a bug report names a version that
 * never existed. versions.json decides now and `ctest -R versions` checks
 * every spelling -- this one, the hex below, and both crates.
 *
 * The hex is major<<16 | minor<<8 | patch, and it is the one a HOST compares
 * when deciding whether a saved project was made by an older build, so a stale
 * one is worse than a stale string: silently wrong rather than visibly wrong.
 */
#define PLUG_VERSION_HEX 0x00000101
#define PLUG_VERSION_STR "0.1.1"

/* A NEW IDENTITY, not a variation on the Trance Gate's. A host catalogues a
 * plugin by this pair, and two plugins sharing one would fight over the same
 * AU registration -- the second one installed simply never appears. */
#define PLUG_UNIQUE_ID 'SpGr'
#define PLUG_MFR_ID 'Grbe'

#define PLUG_URL_STR "https://github.com/graebe/vst-library"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Torben Gräber"
#define PLUG_CLASS_NAME Spectrogram

/*
 * BUNDLE_NAME MUST BE THE BUNDLE'S ACTUAL NAME, not the plugin's display name.
 * iPlug2 builds the bundle identifier from DOMAIN.MFR.type.NAME and looks the
 * bundle up by it to find the Cocoa view; the CMake target is Spectrogram, so
 * the bundle is com.graebe.audiounit.Spectrogram. Get this wrong and the
 * lookup returns NULL, CFBundleCopyBundleURL segfaults the host the moment
 * anything asks for the editor, and auval dies at "VERIFYING CUSTOM UI" --
 * which is where the Trance Gate learned it.
 */
#define BUNDLE_NAME "Spectrogram"
#define BUNDLE_MFR "graebe"
#define BUNDLE_DOMAIN "com"

/* Stereo in, stereo out, passed through bit for bit. The analyzer reads the
 * mono sum; see ProcessBlock. */
#define PLUG_CHANNEL_IO "2-2"
#define SHARED_RESOURCES_SUBPATH "Spectrogram"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0          /* an effect, not an instrument */
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0

/* NO STATE TO SAVE. The picture is not state and there are no parameters yet,
 * so a chunk would serialise nothing. When Range and Speed arrive they are
 * ordinary host parameters and still need no chunk -- the Trance Gate needs one
 * only because a 128-step pattern across 8 slots cannot be parameters. */
#define PLUG_DOES_STATE_CHUNKS 0

#define PLUG_HAS_UI 1
/*
 * 720 x 402: the picture wants width (each column is a pixel of time) and the
 * Ultraviolet system wants a compact, single-purpose window -- "one row of
 * knobs, a readout under each, a hint line at the bottom, done".
 *
 * Across, from the system's 4px grid: space-8 padding either side (32), a 40px
 * gutter for the frequency scale, space-2, and a 608px well holding 606 columns
 * at one column per pixel -- thirteen seconds of history.
 *
 * Down: the title row (control-h, because it holds the Pause button), the well
 * at 258 (a 256px canvas in a 1px frame, one band per pixel), and the hint bar
 * pinned to the bottom edge. The 2 that makes it 402 rather than 400 is that
 * frame: the canvas used to be exactly as tall as the well around it and the
 * border ate the bottom bands.
 *
 * app.css owns the arithmetic and states it in full.
 */
#define PLUG_WIDTH 720
#define PLUG_HEIGHT 402
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
/* Nothing in the window grows with a value, so the host never has to resize
 * it -- unlike the Trance Gate, whose pad grid grows with Length. */
#define PLUG_HOST_RESIZE 0

#define AUV2_ENTRY Spectrogram_Entry
#define AUV2_ENTRY_STR "Spectrogram_Entry"
#define AUV2_FACTORY Spectrogram_Factory
#define AUV2_VIEW_CLASS Spectrogram_View
#define AUV2_VIEW_CLASS_STR "Spectrogram_View"

#define AAX_TYPE_IDS 'SpG1'
#define AAX_PLUG_MFR_STR "graebe"
#define AAX_PLUG_NAME_STR "Spectrogram\nSpGr"
#define AAX_DOES_AUDIOSUITE 0
#define AAX_PLUG_CATEGORY_STR "Effect"

/* An analyzer, so Live and Logic file it with the meters rather than the
 * modulation effects. */
#define VST3_SUBCATEGORY "Fx|Analyzer"

#define CLAP_MANUAL_URL "https://github.com/graebe/vst-library"
#define CLAP_SUPPORT_URL "https://github.com/graebe/vst-library/issues"
#define CLAP_DESCRIPTION "A rolling spectrogram: log frequency, ultraviolet light on black glass"
#define CLAP_FEATURES "audio-effect", "analyzer", "stereo"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

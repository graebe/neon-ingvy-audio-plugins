// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Spectrogram -- iPlug2 build configuration.
 *
 * The second plugin in this repository, and it exists to be looked at rather
 * than listened to: audio passes through untouched and the editor draws a
 * rolling time/frequency picture of it in the Ultraviolet design language.
 *
 * iPlug2 (zlib) + the VST3 SDK (MIT) + CLAP (MIT) + the analyzer's Rust crates
 * and the crates.io crates they link -- realfft, rustfft, rtrb, basedrop,
 * triple_buffer and theirs -- around this repository's GPL-3.0-or-later code.
 * Every one of those is under a licence on deny.toml's allowlist, MPL-2.0's
 * triple_buffer among them, and THIRD_PARTY_LICENSES.md carries their notices
 * (`cargo tree -p spectro-capi -e normal` lists them).
 */
#pragma once

/*
 * "NI Spectrogram" -- NI FOR NEON INGVY, the publisher.
 *
 * This is the name a DAW lists and a user reads. The four-character IDs below
 * are NOT part of it and have not moved: a host stores those in a project, so
 * renaming them would orphan every session that already loads this plugin.
 */
#define PLUG_NAME "NI Spectrogram"
/* The vendor a DAW groups the plugin under. BUNDLE_MFR below is a different
 * thing -- it is part of the bundle IDENTIFIER, which is identity rather than
 * branding, and it stays. */
#define PLUG_MFR "Neon Ingvy"
/*
 * THE DATE SCHEME, v<YYYY.MM.DD>.<subversion>, which AGENTS.md asks for and the
 * Trance Gate moved to first. This used to be 0.1.1 -- and before that it said
 * 1.0.0 here while the crates said 0.1.0, so the plugin and the analyzer inside
 * it disagreed about what they were.
 *
 * versions.json decides; `ctest -R versions` checks every spelling -- this one,
 * the hex below, both crates and all four plists. Three of those consumers
 * cannot hold the display string, so each has a derived form and the
 * derivations are asserted rather than remembered.
 *
 * The hex is major<<16 | minor<<8 | patch, and THE SUBVERSION LIVES IN THE LOW
 * BITS OF THE PATCH -- day*8+sub -- because a second release on one day has to
 * move the packed number or a host cannot tell it from the first. It is the one
 * a HOST compares when deciding whether a saved project was made by an older
 * build, so a stale one is worse than a stale string: silently wrong rather
 * than visibly wrong.
 */
#define PLUG_VERSION_HEX 0x07EA0A35
#define PLUG_VERSION_STR "v2026.10.06.5"

/* A NEW IDENTITY, not a variation on the Trance Gate's. A host catalogues a
 * plugin by this pair, and two plugins sharing one would fight over the same
 * AU registration -- the second one installed simply never appears. */
#define PLUG_UNIQUE_ID 'SpGr'
#define PLUG_MFR_ID 'Grbe'

#define PLUG_URL_STR "https://github.com/graebe/neon-ingvy-audio-plugins"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Torben Gräber"
#define PLUG_CLASS_NAME Spectrogram

/*
 * BUNDLE_NAME MUST BE THE BUNDLE'S ACTUAL NAME, not the plugin's display name.
 * iPlug2 builds the bundle identifier from DOMAIN.MFR.type.NAME and looks the
 * bundle up by it to find the Cocoa view; the CMake target is NISpectrogram, so
 * the bundle is com.graebe.audiounit.NISpectrogram. Get this wrong and the
 * lookup returns NULL, CFBundleCopyBundleURL segfaults the host the moment
 * anything asks for the editor, and auval dies at "VERIFYING CUSTOM UI" --
 * which is where the Trance Gate learned it.
 *
 * PLUG_NAME above is what a user sees; this is what the filesystem sees, and
 * it carries no space because a bundle name with one is a path with one.
 *
 * RENAMED for the publisher's prefix, and the plugin's IDENTITY did not move --
 * PLUG_UNIQUE_ID and PLUG_MFR_ID are what a host stores in a project, and they
 * are untouched -- so sessions relink after a rescan.
 *
 * But the file on disk did move, and an old Spectrogram.component or .vst3 left
 * beside the new one is TWO BUNDLES CLAIMING ONE ID, which hosts report in
 * their own confusing ways. DELETE THE OLD ONE:
 *
 *   rm -rf ~/Library/Audio/Plug-Ins/VST3/Spectrogram.vst3 \
 *          ~/Library/Audio/Plug-Ins/CLAP/Spectrogram.clap \
 *          ~/Library/Audio/Plug-Ins/Components/Spectrogram.component
 */
#define BUNDLE_NAME "NISpectrogram"
#define BUNDLE_MFR "graebe"
#define BUNDLE_DOMAIN "com"

/* Stereo in, stereo out, passed through bit for bit. The analyzer reads the
 * mono sum; see ProcessBlock. */
#define PLUG_CHANNEL_IO "2-2"
#define SHARED_RESOURCES_SUBPATH "NISpectrogram"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0          /* an effect, not an instrument */
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0

/* A CHUNK, because there is state that cannot be a parameter: which buses the
 * window listens to, the view, the comparison and the clash settings -- see
 * SerializeState. With 0 here iPlug2 keeps presets as parameter values only,
 * and a preset would drop all of that. */
#define PLUG_DOES_STATE_CHUNKS 1

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
#define PLUG_HEIGHT 502
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
/* Nothing in the window grows with a value, so the host never has to resize
 * it -- unlike the Trance Gate, whose pad grid grows with Length. */
#define PLUG_HOST_RESIZE 0

#define AUV2_ENTRY NISpectrogram_Entry
#define AUV2_ENTRY_STR "NISpectrogram_Entry"
#define AUV2_FACTORY NISpectrogram_Factory
#define AUV2_VIEW_CLASS NISpectrogram_View
#define AUV2_VIEW_CLASS_STR "NISpectrogram_View"

#define AAX_TYPE_IDS 'SpG1'
#define AAX_PLUG_MFR_STR "Neon Ingvy"
#define AAX_PLUG_NAME_STR "NI Spectrogram\nSpGr"
#define AAX_DOES_AUDIOSUITE 0
#define AAX_PLUG_CATEGORY_STR "Effect"

/* An analyzer, so Live and Logic file it with the meters rather than the
 * modulation effects. */
#define VST3_SUBCATEGORY "Fx|Analyzer"

#define CLAP_MANUAL_URL "https://github.com/graebe/neon-ingvy-audio-plugins"
#define CLAP_SUPPORT_URL "https://github.com/graebe/neon-ingvy-audio-plugins/issues"
#define CLAP_DESCRIPTION "A rolling spectrogram: log frequency, ultraviolet light on black glass"
#define CLAP_FEATURES "audio-effect", "analyzer", "stereo"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

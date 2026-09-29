/*
 * Listen-In -- iPlug2 build configuration.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * The third plugin here, and the first that exists for the sake of ANOTHER
 * plugin. It makes no sound and draws no picture: it passes audio through
 * untouched and publishes a copy on a numbered bus, so that a Spectrogram on a
 * different track can draw it. Several of those, overlaid, is the point.
 *
 * iPlug2 (zlib) + the VST3 SDK (MIT) + CLAP (MIT) + a transport crate with no
 * dependencies at all. Nothing here is copyleft.
 */
#pragma once

#define PLUG_NAME "Listen-In"
#define PLUG_MFR "graebe"

/*
 * 0.1.0 -- and "graebe" above rather than "Neon Ingvy", deliberately.
 *
 * AGENTS.md names the publisher as Neon Ingvy and the repository has already
 * been renamed to match. The two shipping plugins still say graebe, and
 * PLUG_MFR_ID below is what a HOST stores in a saved project -- so changing it
 * relinks nothing and breaks every session that already loaded a Trance Gate.
 * A new plugin is the wrong place to start a house rename: it would leave the
 * three of them disagreeing, which is exactly the state versions.json exists to
 * prevent. When it happens it happens to all three at once.
 *
 * The hex is major<<16 | minor<<8 | patch; versions.json decides both spellings
 * and `ctest -R versions` checks them here, in the crates, and in every plist.
 */
#define PLUG_VERSION_HEX 0x00000100
#define PLUG_VERSION_STR "0.1.0"

/* A NEW IDENTITY, not a variation on either existing one. A host catalogues a
 * plugin by this pair, and two plugins sharing one would fight over the same
 * AU registration -- the second one installed simply never appears. */
#define PLUG_UNIQUE_ID 'LsnI'
#define PLUG_MFR_ID 'Grbe'

#define PLUG_URL_STR "https://github.com/graebe/neon-ingvy-audio-plugins"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Torben Gräber"
#define PLUG_CLASS_NAME ListenIn

/*
 * BUNDLE_NAME IS "ListenIn" WHILE PLUG_NAME IS "Listen-In", and that is not an
 * oversight.
 *
 * iPlug2 builds the bundle identifier from DOMAIN.MFR.type.NAME and looks the
 * bundle up by it to find the Cocoa view; the CMake target is ListenIn, so the
 * bundle is com.graebe.audiounit.ListenIn. Get this wrong and the lookup
 * returns NULL, CFBundleCopyBundleURL segfaults the host the moment anything
 * asks for the editor, and auval dies at "VERIFYING CUSTOM UI" -- which is
 * where the Trance Gate learned it.
 *
 * The hyphen is fine in the DISPLAY name, which is all PLUG_NAME is.
 */
#define BUNDLE_NAME "ListenIn"
#define BUNDLE_MFR "graebe"
#define BUNDLE_DOMAIN "com"

/* Stereo in, stereo out, passed through bit for bit. A mono source is
 * duplicated rather than left silent on the right -- the bus is always stereo,
 * so a receiver never has to ask. */
#define PLUG_CHANNEL_IO "2-2"
#define SHARED_RESOURCES_SUBPATH "ListenIn"

/* ZERO LATENCY, and it has to be: this plugin is meant to sit on tracks you are
 * actually listening to. A tap that made the track it taps arrive late would be
 * a tap nobody leaves in place. Publishing is a memcpy into shared memory; it
 * delays nothing. */
#define PLUG_LATENCY 0
#define PLUG_TYPE 0          /* an effect, not an instrument */
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0

/*
 * A CHUNK, WHICH NEITHER OTHER PLUGIN NEEDED, because the label is TEXT.
 *
 * The slot is an ordinary host parameter and the host saves it for us. The name
 * beside it -- "Bass", "Pad" -- cannot be a parameter: parameters are numbers
 * with a range, and a host that tried to automate this one would be automating
 * nothing. So the state is serialised, parameters first and the label after it,
 * the way the Trance Gate already does for its pattern.
 */
#define PLUG_DOES_STATE_CHUNKS 1

#define PLUG_HAS_UI 1
/*
 * 360 x 232, and small on purpose.
 *
 * This plugin has one job and two controls, and it will sit on half the tracks
 * in a set. A utility that takes a Spectrogram's 720px to show a number and a
 * name is a utility you resent. The Ultraviolet system's compact window is
 * exactly the right shape for it.
 *
 * Across, from the system's 4px grid: space-8 padding either side (32) around a
 * 296px column. Down: the title row, a row holding the slot Select and the name
 * field, the meter under them, and the hint bar pinned to the bottom edge.
 *
 * app.css owns the arithmetic and states it in full.
 */
#define PLUG_WIDTH 360
#define PLUG_HEIGHT 232
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
/* Nothing in the window grows with a value, so the host never has to resize it. */
#define PLUG_HOST_RESIZE 0

#define AUV2_ENTRY ListenIn_Entry
#define AUV2_ENTRY_STR "ListenIn_Entry"
#define AUV2_FACTORY ListenIn_Factory
#define AUV2_VIEW_CLASS ListenIn_View
#define AUV2_VIEW_CLASS_STR "ListenIn_View"

#define AAX_TYPE_IDS 'LsI1'
#define AAX_PLUG_MFR_STR "graebe"
#define AAX_PLUG_NAME_STR "Listen-In\nLsnI"
#define AAX_DOES_AUDIOSUITE 0
#define AAX_PLUG_CATEGORY_STR "Effect"

/* A UTILITY, not an analyzer: it produces no picture of its own. Live and Logic
 * file it with the routing tools, which is where someone looking for it will
 * actually look. */
#define VST3_SUBCATEGORY "Fx|Tools"

#define CLAP_MANUAL_URL "https://github.com/graebe/neon-ingvy-audio-plugins"
#define CLAP_SUPPORT_URL "https://github.com/graebe/neon-ingvy-audio-plugins/issues"
#define CLAP_DESCRIPTION "Publishes a track's audio on a numbered bus for other plugins to read"
#define CLAP_FEATURES "audio-effect", "utility", "stereo"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

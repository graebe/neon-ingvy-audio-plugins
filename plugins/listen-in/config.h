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

#define PLUG_NAME "NI Listen-In"

/*
 * "Neon Ingvy" IS THE PUBLISHER, and BUNDLE_MFR below is still "graebe".
 *
 * PLUG_MFR is a display string -- what a host prints beside the plugin name in
 * its browser. BUNDLE_MFR is a component of the bundle IDENTIFIER (iPlug2
 * assembles DOMAIN.MFR.type.NAME, see IPlug_include_in_plug_hdr.h:94), so it
 * could not carry the space even if it wanted to, and changing it would move
 * every bundle ID in the house for no visible gain. The Trance Gate's rename
 * settled this first; this follows it rather than inventing a second scheme.
 */
#define PLUG_MFR "Neon Ingvy"

/*
 * 0.1.0, and it has never shipped -- which is the whole reason this plugin is
 * "NI Listen-In" while the Trance Gate and the Spectrogram are not yet.
 *
 * PLUG_UNIQUE_ID and PLUG_MFR_ID are what a HOST stores in a saved project, and
 * BUNDLE_NAME is how it finds the bundle again. Renaming any of them is a
 * breaking change for every set that already loads the plugin -- free here,
 * because no set does, and expensive the day after the first release tag. The
 * other two are released, so they keep their names until that migration is
 * done deliberately rather than as a side effect of this one.
 *
 * PLUG_MFR_ID stays 'Grbe' even so: it is the house identifier, shared by all
 * three, and moving it would relink nothing and break the two that shipped.
 *
 * The hex is major<<16 | minor<<8 | patch; versions.json decides both spellings
 * and `ctest -R versions` checks them here, in the crates, and in every plist.
 */
#define PLUG_VERSION_HEX 0x07EA0A33
#define PLUG_VERSION_STR "v2026.10.06.3"

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
 * BUNDLE_NAME IS "NIListenIn" WHILE PLUG_NAME IS "NI Listen-In", and that is
 * not an oversight.
 *
 * iPlug2 builds the bundle identifier from DOMAIN.MFR.type.NAME and looks the
 * bundle up by it to find the Cocoa view; the CMake target is NIListenIn, so
 * the bundle is com.graebe.audiounit.NIListenIn. Get this wrong and the
 * lookup returns NULL, CFBundleCopyBundleURL segfaults the host the moment
 * anything asks for the editor, and auval dies at "VERIFYING CUSTOM UI" --
 * which is where the Trance Gate learned it.
 *
 * The space and the hyphen are fine in the DISPLAY name, which is all
 * PLUG_NAME is. Neither belongs in an identifier.
 */
#define BUNDLE_NAME "NIListenIn"
#define BUNDLE_MFR "graebe"
#define BUNDLE_DOMAIN "com"

/* Stereo in, stereo out, passed through bit for bit. A mono source is
 * duplicated rather than left silent on the right -- the bus is always stereo,
 * so a receiver never has to ask. */
#define PLUG_CHANNEL_IO "2-2"
#define SHARED_RESOURCES_SUBPATH "NIListenIn"

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

#define AUV2_ENTRY NIListenIn_Entry
#define AUV2_ENTRY_STR "NIListenIn_Entry"
#define AUV2_FACTORY NIListenIn_Factory
#define AUV2_VIEW_CLASS NIListenIn_View
#define AUV2_VIEW_CLASS_STR "NIListenIn_View"

#define AAX_TYPE_IDS 'LsI1'
#define AAX_PLUG_MFR_STR "Neon Ingvy"
#define AAX_PLUG_NAME_STR "NI Listen-In\nLsnI"
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

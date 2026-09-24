/*
 * Trance Gate -- iPlug2 build configuration.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * WHY iPlug2 AND NOT JUCE. JUCE is GPLv3-or-commercial, and it was the only
 * reason this repository could not be MIT. Nothing here needed it: of the 62
 * distinct juce:: symbols the old build used, 26 were drawing, 8 were the
 * format wrappers and 12 were utility -- and none of them were DSP, which has
 * been Rust for some time. iPlug2 is zlib-licensed and covers all of it, and
 * Steinberg relicensed the VST3 SDK to MIT, so the format costs nothing
 * either.
 */
#pragma once

#define PLUG_NAME "Trance Gate"
#define PLUG_MFR "graebe"
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_VERSION_STR "1.0.0"

/* THE FOUR-CHARACTER IDS ARE THE PLUGIN'S IDENTITY and they are carried over
 * from the JUCE build deliberately: a host that catalogued this plugin
 * catalogued 'TrGt' by 'Grbe', and the AU in particular is found by exactly
 * that pair. Keeping them means an existing AU slot finds this build. */
#define PLUG_UNIQUE_ID 'TrGt'
#define PLUG_MFR_ID 'Grbe'

#define PLUG_URL_STR "https://github.com/graebe/schwung-trance-gate"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Torben Gräber"
#define PLUG_CLASS_NAME TranceGate

/*
 * BUNDLE_NAME MUST BE THE BUNDLE'S ACTUAL NAME, not the plugin's display
 * name. iPlug2 builds the bundle identifier from DOMAIN.MFR.type.NAME and
 * looks the bundle up by it to find the Cocoa view; the CMake target is
 * TranceGateIP, so the bundle is com.graebe.audiounit.TranceGateIP. With
 * "Trance Gate" here the lookup returned NULL and CFBundleCopyBundleURL
 * segfaulted the host the moment anything asked for the editor -- auval died
 * with SIGSEGV at "VERIFYING CUSTOM UI" and Logic would have too.
 *
 * PLUG_NAME above is what a user sees; this is what the filesystem sees.
 */
#define BUNDLE_NAME "TranceGateIP"
#define BUNDLE_MFR "graebe"
#define BUNDLE_DOMAIN "com"

/* Stereo in, stereo out. The engine has a split-channel path and applies one
 * gain to both, so the layout is not a DSP question. */
#define PLUG_CHANNEL_IO "2-2"
#define SHARED_RESOURCES_SUBPATH "TranceGate"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0          /* an effect, not an instrument */
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0

/* STATE CHUNKS, BECAUSE THE PATTERN IS NOT A PARAMETER. Twelve values have
 * host parameters behind them; the pattern, the ties, the per-step depths and
 * all eight slots do not, and 128 x 8 of them never will. They travel as the
 * engine's own state blob -- the same text the Move module writes, which is
 * what makes a patch portable between the two. */
#define PLUG_DOES_STATE_CHUNKS 1

/* A WEBVIEW EDITOR. The UI is a Solid app in ui/, built by Vite into
 * resources/web and copied into the bundle. It is not a stylistic choice:
 * the same markup is what a browser build would run, which is the direction
 * the rest of this project is pointed. */
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 900
#define PLUG_HEIGHT 560
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0

#define AUV2_ENTRY TranceGate_Entry
#define AUV2_ENTRY_STR "TranceGate_Entry"
#define AUV2_FACTORY TranceGate_Factory
#define AUV2_VIEW_CLASS TranceGate_View
#define AUV2_VIEW_CLASS_STR "TranceGate_View"

#define AAX_TYPE_IDS 'TGt1'
#define AAX_PLUG_MFR_STR "graebe"
#define AAX_PLUG_NAME_STR "Trance Gate\nTrGt"
#define AAX_DOES_AUDIOSUITE 0
#define AAX_PLUG_CATEGORY_STR "Modulation"

#define VST3_SUBCATEGORY "Fx|Modulation"

#define CLAP_MANUAL_URL "https://github.com/graebe/schwung-trance-gate"
#define CLAP_SUPPORT_URL "https://github.com/graebe/schwung-trance-gate/issues"
#define CLAP_DESCRIPTION "Tempo-locked step gate with a per-step ADSR, ties and eight pattern slots"
#define CLAP_FEATURES "audio-effect", "stereo", "utility"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

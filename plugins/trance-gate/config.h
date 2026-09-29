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

#define PLUG_URL_STR "https://github.com/graebe/vst-library"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Torben Gräber"
#define PLUG_CLASS_NAME TranceGate

/*
 * BUNDLE_NAME MUST BE THE BUNDLE'S ACTUAL NAME, not the plugin's display
 * name. iPlug2 builds the bundle identifier from DOMAIN.MFR.type.NAME and
 * looks the bundle up by it to find the Cocoa view; the CMake target is
 * TranceGate, so the bundle is com.graebe.audiounit.TranceGate. With
 * "Trance Gate" here the lookup returned NULL and CFBundleCopyBundleURL
 * segfaulted the host the moment anything asked for the editor -- auval died
 * with SIGSEGV at "VERIFYING CUSTOM UI" and Logic would have too.
 *
 * PLUG_NAME above is what a user sees; this is what the filesystem sees.
 *
 * RENAMED FROM "TranceGateIP" when the plugin stopped carrying its framework
 * in its name. The plugin's IDENTITY did not move -- PLUG_UNIQUE_ID and
 * PLUG_MFR_ID below are what a host stores in a project -- so sessions relink
 * after a rescan. But the file on disk did, so an old TranceGateIP.component
 * or .vst3 left beside the new one is two bundles claiming one ID, which
 * hosts report in their own confusing ways. Delete the old one.
 */
#define BUNDLE_NAME "TranceGate"
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
/*
 * 856, WHICH IS 32 MORE THAN THE JUCE EDITOR'S 824.
 *
 * The original was space-8 * 2 + StepGridView::width (760) = 824, and the band
 * holding the plots was 760 as well -- but the tab strip takes 24 off the
 * band's right edge with 8 of gap, so the PLOT drew at 728 against a 760 grid
 * of pads. The JUCE editor accepted that ("a rhyme that thin is worth less
 * than a view switch you can find").
 *
 * Aligning them costs those 32 pixels and nothing else: the band is 792, the
 * plot is 760, the tab strip survives, and every other number in the layout is
 * the original's. See PLOT_W / BAND_W in ui/src/App.jsx, which own the
 * arithmetic.
 *
 * The height is 568 (kGridY) + one row of pads + space-6 + the hint bar at the
 * default 16 steps, and grows with Length -- see kMsgRows.
 */
#define PLUG_WIDTH 856
#define PLUG_HEIGHT 660
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
/* The window grows with Length -- see kMsgRows. */
#define PLUG_HOST_RESIZE 1

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

#define CLAP_MANUAL_URL "https://github.com/graebe/vst-library"
#define CLAP_SUPPORT_URL "https://github.com/graebe/vst-library/issues"
#define CLAP_DESCRIPTION "Tempo-locked step gate with a per-step ADSR, ties and eight pattern slots"
#define CLAP_FEATURES "audio-effect", "stereo", "utility"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

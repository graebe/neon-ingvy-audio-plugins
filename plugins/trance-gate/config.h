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
#define PLUG_VERSION_HEX 0x00010100
#define PLUG_VERSION_STR "1.1.0"

/* THE FOUR-CHARACTER IDS ARE THE PLUGIN'S IDENTITY and they are carried over
 * from the JUCE build deliberately: a host that catalogued this plugin
 * catalogued 'TrGt' by 'Grbe', and the AU in particular is found by exactly
 * that pair. Keeping them means an existing AU slot finds this build. */
#define PLUG_UNIQUE_ID 'TrGt'
#define PLUG_MFR_ID 'Grbe'

#define PLUG_URL_STR "https://github.com/graebe/neon-ingvy-audio-plugins"
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

/* STATE CHUNKS, BECAUSE THE PATTERN IS NOT A PARAMETER. Fourteen values have
 * host parameters behind them; the pattern, the ties, the per-step depths, the
 * fade's arrival order and all eight slots do not, and 128 x 8 of them never
 * will. They travel as the
 * engine's own state blob -- the same text the Move module writes, which is
 * what makes a patch portable between the two. */
#define PLUG_DOES_STATE_CHUNKS 1

/* A WEBVIEW EDITOR. The UI is a Solid app in ui/, built by Vite into
 * resources/web and copied into the bundle. It is not a stylistic choice:
 * the same markup is what a browser build would run, which is the direction
 * the rest of this project is pointed. */
#define PLUG_HAS_UI 1
/*
 * 824 AND 736, AND BOTH NUMBERS ARE DECIDED BY THE PADS.
 *
 * WIDTH. Sixteen pads of --step (40) with --s2 (8) between them is 760, and
 * --step is a design-system token on a 4px grid -- so the pad grid decides the
 * content width and everything else in the window is measured against it. 32 of
 * padding each side makes 824.
 *
 * It was 856 for a while, and that was buying one thing: the tab strip took 24
 * off the plot band's right edge with 8 of gap, so the plot drew 32 narrower
 * than the pads under it, and 32 more pixels of window gave the strip a column
 * of its own. The cost was a second right-hand edge -- the band ended at 824
 * and every other element at 792 -- so the window had one padding on the left
 * and two different ones on the right. The strip is laid OVER the plot now,
 * which buys the alignment for nothing and gives the 32 back.
 *
 * HEIGHT. 644 (kGridY) + one row of pads + space-6 + the hint bar, at the
 * default 16 steps, and it grows with Length -- see kMsgRows.
 *
 * 644 and not the old 568 because there are three panels in the right column
 * now rather than two. The third one is paid for by the panels themselves: their
 * titles run up the left edge instead of sitting above the knobs, which takes a
 * panel from 172 to 140 -- so a third costs 76px of window where a fourth
 * horizontal-titled one would have cost 148.
 *
 * Mirrored by `main`'s width and padding-top in ui/src/app.css and by DESIGN_W
 * and designH in ui/src/App.jsx. All of them have to agree, or the page is
 * scaled against a width it does not have and the window is the wrong height
 * for what is in it.
 */
#define PLUG_WIDTH 824
#define PLUG_HEIGHT 736
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

#define CLAP_MANUAL_URL "https://github.com/graebe/neon-ingvy-audio-plugins"
#define CLAP_SUPPORT_URL "https://github.com/graebe/neon-ingvy-audio-plugins/issues"
#define CLAP_DESCRIPTION "Tempo-locked step gate with a per-step ADSR, ties and eight pattern slots"
#define CLAP_FEATURES "audio-effect", "stereo", "utility"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

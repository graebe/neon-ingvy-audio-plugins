/*
 * NI Side-Chain -- iPlug2 build configuration.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#pragma once

#define PLUG_NAME "NI Side-Chain"
#define PLUG_MFR "Neon Ingvy"
/*
 * THE VERSION IS A DATE, AND THREE CONSUMERS CANNOT HOLD IT LITERALLY.
 *
 * versions.json decides, in the scheme v<YYYY.MM.DD>.<subversion>. This string
 * is what a DAW shows and carries it verbatim. The other three cannot:
 *
 *   Cargo.toml       strict semver -- so the crates say 2026.9.29+1, the
 *                    subversion riding as build metadata: legal, and preserved.
 *   Info.plist       CFBundleShortVersionString is up to three integers, so the
 *                    plists say 2026.9.29.
 *   PLUG_VERSION_HEX major<<16 | minor<<8 | patch, and 16/8/8 bits.
 *
 * The hex is what a host compares to decide whether a saved project came from an
 * older build, so it must move on every release -- a second release on the same
 * day included, which is what the subversion is for. There is no fourth field,
 * so it rides in the low bits of the patch:
 *
 *     patch = day * 8 + subversion            2026.09.29.1 -> 29*8+1 = 233
 *     hex   = year<<16 | month<<8 | patch     -> 0x07EA09E9
 *
 * The derivations are asserted by `ctest -R versions` rather than left to a
 * human. See plugins/trance-gate/config.h, which explains the scheme at length.
 */
#define PLUG_VERSION_HEX 0x07EA09EA
#define PLUG_VERSION_STR "v2026.09.29.2"

/*
 * THE FOUR-CHARACTER IDS ARE THE PLUGIN'S IDENTITY -- what a host stores in a
 * project, and how an AU is found. A new plugin gets new ones and then never
 * changes them: 'SdCh' by 'Grbe', the house manufacturer code the other two
 * plugins already use.
 */
#define PLUG_UNIQUE_ID 'SdCh'
#define PLUG_MFR_ID 'Grbe'

#define PLUG_URL_STR "https://github.com/graebe/neon-ingvy-audio-plugins"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Torben Gräber"
#define PLUG_CLASS_NAME SideChain

/*
 * BUNDLE_NAME MUST BE THE BUNDLE'S ACTUAL NAME, not the plugin's display name,
 * and the two differ here on purpose.
 *
 * iPlug2 builds the bundle identifier from DOMAIN.MFR.type.NAME and looks the
 * bundle up by it to find the Cocoa view. The CMake target is NISideChain -- a CMake
 * target cannot hold a space -- so the bundle is com.graebe.audiounit.NISideChain.
 * With "NI Side-Chain" here the lookup returns NULL and CFBundleCopyBundleURL
 * segfaults the host the moment anything asks for the editor; that is how the
 * Trance Gate found this out, with auval dying at "VERIFYING CUSTOM UI".
 *
 * PLUG_NAME above is what a user sees ("NI Side-Chain", the house naming convention).
 * This is what the filesystem sees.
 */
#define BUNDLE_NAME "NISideChain"
#define BUNDLE_MFR "graebe"
#define BUNDLE_DOMAIN "com"

/*
 * STEREO IN, STEREO OUT, PLUS A SIDECHAIN BUS -- and this spelling is the one
 * genuinely new piece of shell plumbing in the repository. Neither existing
 * plugin has an aux input.
 *
 * The form is "main.aux-out", and the full list is what
 * Examples/IPlugSideChain/config.h declares: every combination of mono or
 * stereo main, mono or stereo aux, mono or stereo out, plus the two no-aux
 * cases so a host that offers no sidechain still instantiates. Omitting "2-2"
 * would make it un-loadable on a plain stereo track, which is where most of
 * its uses are.
 *
 * In Live this surfaces as a sidechain source selector on the device.
 */
#define PLUG_CHANNEL_IO "1-1 1.1-1 1.2-1 1.2-2 2.1-1 2.1-2 2-2 2.2-2"
#define SHARED_RESOURCES_SUBPATH "NISideChain"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0          /* an effect, not an instrument */

/*
 * MIDI IN, WHICH MAKES THE AU AN `aumf` AND NOT AN `aufx`.
 *
 * IPlugAU.r:84-89 picks kAudioUnitType_MusicEffect from PLUG_TYPE 0 plus this,
 * so the AU registers as 'aumf' -- and resources/NISideChain-AU-Info.plist says so
 * literally, because iPlug2 substitutes nothing there and `ctest -R versions`
 * asserts it. The `sc_au` test looks for the aumf/SdCh/Grbe triple.
 *
 * That is the correct AU type for an effect that takes notes, not a workaround:
 * an 'aufx' is declared as having no event input, and a host is entitled to
 * believe it.
 *
 * THE LIVE CAVEAT, which the README and the editor both state: Live does not
 * route MIDI to a plugin sitting on an AUDIO track. MIDI mode therefore fires
 * when Side-Chain sits on a MIDI track after an instrument. Cycle is the default
 * source for exactly that reason, and the editor reports which source is
 * actually firing rather than leaving a silent trigger to look like zero depth.
 */
#define PLUG_DOES_MIDI_IN 1
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0

/*
 * NO STATE CHUNKS, AND THAT IS A DESIGN DECISION RATHER THAN A DEFAULT.
 *
 * The Trance Gate needs them because its pattern is 128 steps across 8 slots
 * and none of that can be a host parameter. Every last thing Side-Chain holds IS a
 * host parameter -- fifteen of them -- so the whole of its state travels in the
 * automation lanes, where the host can see it, automate it and undo it.
 *
 * The cost of keeping it that way is that the shape editor's handles have to be
 * parameters too, which is what they are: dragging one is BeginInformHost /
 * SendParameterValueFromUI / End, so it lands in Live's undo history like a
 * knob. That is worth more than a free-form curve would be.
 *
 * The project state still passes through SideChain::SerializeState, which puts
 * shell_state.h's versioned header in front of the parameters. Presets -- all
 * this flag governs in VST3, AU and CLAP -- are parameter values, which here is
 * the whole state.
 */
#define PLUG_DOES_STATE_CHUNKS 0

/* A WEBVIEW EDITOR: a Solid app in ui/, built by Vite into resources/web and
 * copied into the bundle. The same markup a browser build would run. */
#define PLUG_HAS_UI 1

/*
 * 760 WIDE, WHICH IS THE PLOT WIDTH PLUS THE WINDOW PADDING.
 *
 * The two wells -- the shape editor and the signal diagram -- are the design,
 * and they are the same width because they share one x axis and one time span.
 * 696 of plot plus space-8 (32) on each side is 760. Every other number follows
 * from that; see DESIGN_W in ui/src/App.jsx, which owns the arithmetic.
 *
 * The height is fixed at 604 and nothing here grows with a parameter the way the
 * Trance Gate's pad grid grows with Length. PLUG_HOST_RESIZE is still on,
 * because the editor scales itself to whatever viewport Live hands it and then
 * reports the height that scale needs.
 *
 * 760 AND 604 HAVE TO AGREE WITH DESIGN_W / DESIGN_H IN ui/src/App.jsx AND WITH
 * `main` IN ui/src/app.css. Three spellings of one number, and the page is
 * scaled against whichever of them the editor believes.
 */
#define PLUG_WIDTH 760
#define PLUG_HEIGHT 604
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 1

#define AUV2_ENTRY NISideChain_Entry
#define AUV2_ENTRY_STR "NISideChain_Entry"
#define AUV2_FACTORY NISideChain_Factory
#define AUV2_VIEW_CLASS NISideChain_View
#define AUV2_VIEW_CLASS_STR "NISideChain_View"

#define AAX_TYPE_IDS 'Pmp1'
#define AAX_PLUG_MFR_STR "Neon Ingvy"
#define AAX_PLUG_NAME_STR "NI Side-Chain\nSdCh"
#define AAX_DOES_AUDIOSUITE 0
#define AAX_PLUG_CATEGORY_STR "Dynamics"

#define VST3_SUBCATEGORY "Fx|Dynamics"

#define CLAP_MANUAL_URL "https://github.com/graebe/neon-ingvy-audio-plugins"
#define CLAP_SUPPORT_URL "https://github.com/graebe/neon-ingvy-audio-plugins/issues"
#define CLAP_DESCRIPTION "Sidechain ducker triggered by the transport, a MIDI note or a key input, with a live shape editor"
#define CLAP_FEATURES "audio-effect", "stereo", "compressor"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Ultraviolet 1.2.0 -- GENERATED, DO NOT EDIT.
 *
 * scripts/gen-tokens.mjs wrote this from design/scheme/project/tokens.json
 * and the motion constants of design/scheme/project/components/ground.js.
 * After re-syncing the design, run
 *
 *     node scripts/gen-tokens.mjs
 *
 * and commit what it writes. tests/ui_tokens_native.test.mjs fails while this
 * file differs from what the generator would write, and while any other
 * source of the native UI spells a colour of its own: this is the one file in
 * it that may.
 *
 * Names are the system's, camel-cased: bg-000 is bg000, ink-muted inkMuted,
 * control-h controlH. Lengths are CSS px as floats; a text style's tracking
 * is in em, as the system states it. The comments are the system's usage
 * notes, which say what a token is FOR.
 */
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace uv::tok
{

/* The system these values are, and its release. */
inline constexpr const char* systemName = "Ultraviolet";
inline constexpr const char* release = "1.2.0";

/* ============================================================ colour == */

/*
 * One theme. Plugins live inside Ableton's dark session view and never follow
 * the host theme. Data colours (spec-*, plot-*) colour pictures of a signal
 * and nothing else: never a control, a panel or text.
 */

/*
 * Each theme twice: the 0xAARRGGBB words, which constexpr tables can use, and
 * the juce::Colour constants built from them, which everything else uses.
 * `colour` is the theme the plugins draw in: the system has one.
 */
namespace argb
{
namespace ultraviolet
{
inline constexpr juce::uint32 bg000 = 0xff060410;
inline constexpr juce::uint32 bgDot = 0xff2a1e4a;
inline constexpr juce::uint32 bg100 = 0xff0c0818;
inline constexpr juce::uint32 bg200 = 0xff140e24;
inline constexpr juce::uint32 bg300 = 0xff1d1533;
inline constexpr juce::uint32 line100 = 0xff1d1533;
inline constexpr juce::uint32 line200 = 0xff3a2a66;
inline constexpr juce::uint32 ink = 0xfff3ecff;
inline constexpr juce::uint32 inkMuted = 0xffb9a3e6;
inline constexpr juce::uint32 inkDim = 0xff7a5fb5;
inline constexpr juce::uint32 uv = 0xffefe3ff;
inline constexpr juce::uint32 uvDeep = 0xffa259ff;
inline constexpr juce::uint32 uvGlow = 0x80a259ff;
inline constexpr juce::uint32 onUv = 0xff5d00d6;
inline constexpr juce::uint32 amber = 0xffffb000;
inline constexpr juce::uint32 red = 0xffff4d4d;
inline constexpr juce::uint32 spec0 = 0xff060410;
inline constexpr juce::uint32 spec1 = 0xff2a1e4a;
inline constexpr juce::uint32 spec2 = 0xff6a2fd0;
inline constexpr juce::uint32 spec3 = 0xffa259ff;
inline constexpr juce::uint32 spec4 = 0xffefe3ff;
inline constexpr juce::uint32 plotFill = 0xff1d1533;
inline constexpr juce::uint32 plotGhost = 0xff3a2a66;
inline constexpr juce::uint32 plotDry = 0xff6b6b78;
inline constexpr juce::uint32 plotKey = 0xffffb000;
}
}

/* The "Ultraviolet" theme. */
namespace ultraviolet
{
/* Window ground: the plugin's outermost surface. Everything sits on this. */
inline const juce::Colour bg000 { argb::ultraviolet::bg000 };
/*
 * The dot of the dot-paper pattern on the window ground: 1px dots at a 12px
 * (space-3) pitch, under a 5% uv-deep uniform noise. Grows, brightens and
 * gains grain on a Ground wave peak. Only on the window ground, never inside
 * a panel or well.
 */
inline const juce::Colour bgDot { argb::ultraviolet::bgDot };
/*
 * Panel: a grouped section of controls inside the window (envelope,
 * sequencer).
 */
inline const juce::Colour bg100 { argb::ultraviolet::bg100 };
/*
 * Control well: knob disc, slider rail bed, select field, a step that is off.
 */
inline const juce::Colour bg200 { argb::ultraviolet::bg200 };
/*
 * Raised well: a well on hover, a pressed button's ground, a list row under
 * the pointer.
 */
inline const juce::Colour bg300 { argb::ultraviolet::bg300 };
/*
 * Hairline: dividers between panels and around wells. Decorative, never
 * carries meaning.
 */
inline const juce::Colour line100 { argb::ultraviolet::line100 };
/*
 * Rail: the unlit part of a knob arc or slider track. Marks range, not value.
 */
inline const juce::Colour line200 { argb::ultraviolet::line200 };
/*
 * Primary text: values, readouts, titles. Near-white with a violet cast. On
 * bg-000 to bg-300 (15:1+).
 */
inline const juce::Colour ink { argb::ultraviolet::ink };
/*
 * Secondary text: parameter labels, units, the hint bar. On bg-000 to bg-300
 * (8:1+).
 */
inline const juce::Colour inkMuted { argb::ultraviolet::inkMuted };
/*
 * Disabled text and inactive marks (ring segments, tick marks). Not for
 * running text: 3.1:1 on bg-300, use at 12px+ bold or for marks only.
 */
inline const juce::Colour inkDim { argb::ultraviolet::inkDim };
/*
 * The signal, as near-white violet: lit steps, value arcs (and the detent
 * tick a value sits on), LED, focus, primary button fill. It is the bright
 * core; the violet lives in its halo (uv-deep).
 */
inline const juce::Colour uv { argb::ultraviolet::uv };
/*
 * The saturated violet. Never a fill on its own: it is the glow around every
 * lit uv element (glow-led, arc and fill drop-shadows) and the tint of the
 * ground noise.
 */
inline const juce::Colour uvDeep { argb::ultraviolet::uvDeep };
/*
 * Halo around a lit element (uv-deep at 50%): LED, lit and playhead steps,
 * focus. Alpha only; never a fill.
 */
inline const juce::Colour uvGlow { argb::ultraviolet::uvGlow };
/*
 * Text and glyphs on a uv fill (primary button, a lit icon button, a lit
 * step's index): deep ultraviolet, uv-deep's hue darkened, 7.0:1 on uv, so a
 * lit control keeps its violet in its own letters. Never on red: the record
 * button's glyph on its red fill is bg-000 (on-uv on red is 2.6:1).
 */
inline const juce::Colour onUv { argb::ultraviolet::onUv };
/*
 * Warning state: record armed, clipping about to happen, an unsaved patch.
 * Small marks only, at most one per window. Also a signal: inside a PlotWell
 * its value is plot-key, the reference a processor reacts to, drawn as a
 * band. A band is the signal, a rule or a mark is still the warning.
 */
inline const juce::Colour amber { argb::ultraviolet::amber };
/* Error/clip: a meter past 0 dB, a failed paste. Never decorative. */
inline const juce::Colour red { argb::ultraviolet::red };
/*
 * Spectrogram ramp, stop 0 of 5: silence. The value of bg-000, so a silent
 * spectrogram is the window's own ground. The five stops are one sequence,
 * sampled between by magnitude; a stop means a position on the ramp, not a
 * colour of its own. Data colour: only in a picture of a signal.
 */
inline const juce::Colour spec0 { argb::ultraviolet::spec0 };
/*
 * Spectrogram ramp, stop 1: the quietest band that shows. The value of
 * bg-dot. Data colour.
 */
inline const juce::Colour spec1 { argb::ultraviolet::spec1 };
/*
 * Spectrogram ramp, stop 2: the middle of the range. The one stop that
 * appears nowhere else in the system. Data colour.
 */
inline const juce::Colour spec2 { argb::ultraviolet::spec2 };
/*
 * Spectrogram ramp, stop 3: loud. The value of uv-deep, here as data rather
 * than light: uv-deep's never-a-fill rule governs the interface, and the ramp
 * is a picture of sound.
 */
inline const juce::Colour spec3 { argb::ultraviolet::spec3 };
/*
 * Spectrogram ramp, stop 4: the loudest band, at full scale. The value of uv,
 * so the loudest part of the picture is lit in the system's own signal.
 */
inline const juce::Colour spec4 { argb::ultraviolet::spec4 };
/*
 * The area under a curve in a PlotWell (an envelope, the gate across one
 * cycle). The value of bg-300, so the curve's own 2px uv line is the only
 * light in the well. Data colour.
 */
inline const juce::Colour plotFill { argb::ultraviolet::plotFill };
/*
 * A reference curve behind the one that plays, 1px: the envelope as dialled
 * behind what the gate leaves of it. The value of line-200, a rail: it marks
 * what could be, not what is. Data colour, only in a PlotWell.
 */
inline const juce::Colour plotGhost { argb::ultraviolet::plotGhost };
/*
 * The dry input behind a processed trace, at half opacity under the uv one.
 * Deliberately achromatic, the one grey in the system, so it reads as 'not
 * the signal': the violet is what the plugin did, the grey is what came in.
 * Data colour, only in a PlotWell.
 */
inline const juce::Colour plotDry { argb::ultraviolet::plotDry };
/*
 * The reference a processor reacts to, behind everything it shows: the kick
 * behind a ducker's input and output, at 30% opacity with a 1px edge at 90%
 * over the dry. The value of amber, so the one warm colour in a well is the
 * thing to time against. A band, never a rule: amber as a rule or a mark is a
 * warning. Data colour, only in a PlotWell.
 */
inline const juce::Colour plotKey { argb::ultraviolet::plotKey };
}

namespace colour = ultraviolet;

/* ========================================================= spacing == */

/*
 * A 4px grid. Controls snap to it; the knob (48) and step (40) are multiples.
 */
namespace space
{
/* Gap between a rail and its bed; LED to its label. */
inline constexpr float space1 = 4.0f;
/* Gap between steps in a grid; label to control; padding inside a readout. */
inline constexpr float space2 = 8.0f;
/* Padding inside buttons and selects. */
inline constexpr float space3 = 12.0f;
/* Gap between controls in a row; panel padding. */
inline constexpr float space4 = 16.0f;
/* Gap between control groups (envelope, gate). */
inline constexpr float space6 = 24.0f;
/* Window padding; gap between the big readout and the control block. */
inline constexpr float space8 = 32.0f;
}

/* ========================================================== radius == */

/* Square by default. Round only what physically turns. */
namespace radius
{
/* Panels, wells, buttons, steps, readouts, selects. The default. */
inline constexpr float radius0 = 0.0f;
/*
 * Slider thumb, LED housing, the caret in a select. The only softening
 * allowed on a rectangle.
 */
inline constexpr float radius1 = 2.0f;
/* Knob discs, LED lens, ring segments' end caps. */
inline constexpr float radiusFull = 999.0f;
}

/* ============================================================ size == */

/*
 * Control sizes, in px. Keep them: a plugin is a machine, and its knobs are
 * all the same knob.
 */
namespace size
{
/* Knob disc diameter, label above, readout below. */
inline constexpr float knob = 48.0f;
/*
 * One larger knob per window at most, for the parameter the plugin is about.
 */
inline constexpr float knobLg = 64.0f;
/* Sequencer step, square. */
inline constexpr float step = 40.0f;
/* Height of buttons, selects and readouts. */
inline constexpr float controlH = 28.0f;
/* Knob arc and slider track stroke. */
inline constexpr float rail = 2.0f;
/* Every border. */
inline constexpr float hairline = 1.0f;
}

/* ========================================================== stroke == */

namespace stroke
{
/* Knob arc, slider track, ring segments. */
inline constexpr float strokeRail = 2.0f;
/* Borders and dividers. */
inline constexpr float strokeHair = 1.0f;
}

/* ============================================================ shadow == */

/*
 * No drop shadows: nothing floats. The only shadow is light, and the light is
 * violet around a white core.
 */

/* One layer of a CSS box-shadow: offsets, blur and spread in px, and its
 * colour. A shadow is its layers in the order CSS lists them. */
struct ShadowLayer
{
    float x, y, blur, spread;
    juce::uint32 argb;
};

namespace shadow
{
/*
 * Halo on every lit element: LED on, a lit step, the playhead, a primary
 * button.
 */
inline constexpr ShadowLayer glowLed[] = {
    { 0.0f, 0.0f, 10.0f, 0.0f, argb::ultraviolet::uvDeep },
    { 0.0f, 0.0f, 2.0f, 0.0f, argb::ultraviolet::uv },
};
/* Keyboard focus ring on any control. */
inline constexpr ShadowLayer glowFocus[] = {
    { 0.0f, 0.0f, 0.0f, 1.0f, argb::ultraviolet::uv },
    { 0.0f, 0.0f, 8.0f, 0.0f, argb::ultraviolet::uvGlow },
};
}

/* ============================================================== type == */

/*
 * One family, monospace, loaded from Google Fonts (previews) or bundled with
 * the plugin (JetBrains Mono, OFL). Digits must be tabular: a readout must
 * not jitter while a knob turns.
 */

/* A text style as the system states it: size and line height in px (CSS
 * px, so the size is the em square), weight as CSS numbers it, tracking in
 * em. Uppercase is not a token; the styles that set it say so in their
 * usage notes. */
struct TextStyle
{
    float size, lineHeight;
    int weight;
    float tracking;
};

namespace type
{
/* The family the plugins bundle and draw in: the first of the system's
 * stack, which names the fallbacks for a page that cannot bundle it. */
inline constexpr const char* family = "JetBrains Mono";
inline constexpr const char* familyStack = "\"JetBrains Mono\", \"SF Mono\", Menlo, Consolas, monospace";

/*
 * The one big number a plugin is about (step count, BPM, dB). ink on bg-000.
 */
inline constexpr TextStyle readout { 28.0f, 32.0f, 500, -0.01f };
/* Plugin or panel name, uppercase. ink-muted. */
inline constexpr TextStyle title { 12.0f, 16.0f, 500, 0.08f };
/* Parameter label above a knob or slider, uppercase. ink-muted. */
inline constexpr TextStyle label { 11.0f, 14.0f, 500, 0.1f };
/* Value readout under a control, select text, list rows. ink. */
inline constexpr TextStyle value { 13.0f, 16.0f, 400, 0.0f };
/* Button text. ink, or on-uv on a primary button. */
inline constexpr TextStyle button { 12.0f, 16.0f, 500, 0.04f };
/*
 * The hint bar at the bottom of a window, and a PlotWell's caption.
 * ink-muted, with the verb or an info string's name in ink.
 */
inline constexpr TextStyle hint { 10.0f, 14.0f, 400, 0.02f };

/* Every weight a style uses: the faces a plugin has to bundle. */
inline constexpr int weights[] = { 400, 500 };
}

/* ============================================================ motion == */

/*
 * "Controls never animate." The one thing that moves is the Ground, and these
 * are its reference implementation's options (ground.js, UVGround.defaults and
 * UVGround.beatDefaults): durations in seconds, rates in Hz or per second,
 * lengths in px, opacities 0..1. The ground's colours are not repeated here:
 * ground.js names bg-000, bg-dot and uv-deep, and the generator checks that
 * it still does.
 */
namespace motion
{
/* The field: UVGround.defaults. */
namespace ground
{
inline constexpr float pitch = 12.0f;  // dot pitch, px (space-3)
inline constexpr float cell = 6.0f;  // simulation grid, px (half a pitch)
inline constexpr float speed = 100.0f;  // c, px/s
inline constexpr float freq = 1.5f;  // f₀ of the source wavelet, Hz → wavelength c/f₀ ≈ 67 px; a ring on every beat (90–140 BPM) does not cancel the last
inline constexpr float tau = 3.5f;  // amplitude e-folding time, s (γ = 2/τ); the field rings out in ~20 s
inline constexpr float gain = 65.0f;  // source strength A: one downbeat peaks near u ≈ 0.9
inline constexpr bool border = true;  // the window border emits as well as reflects
inline constexpr float dt = 0.016666666666666666f;  // fixed simulation step, s
inline constexpr float maxSteps = 6.0f;  // per frame, after a stall
inline constexpr float rest = 0.004f;  // below this everywhere (and no ring active) the field is reset and the timer stops
inline constexpr float dotR = 1.0f;  // dot radius at rest, px
inline constexpr float dotDR = 0.4f;  // ± radius at |v| = 1
inline constexpr float dotDA = 0.3f;  // ± relative dot opacity at |v| = 1
inline constexpr float grainBase = 0.5f;  // grain layer opacity at rest (tile mean 10% → 5% effective)
inline constexpr float grainK = 0.8f;  // grain gain per unit v
inline constexpr float grainMin = 0.2f;  // → 2% effective in the deepest valley
inline constexpr float grainMax = 0.9f;  // → 9% effective on the highest peak
inline constexpr float fps = 30.0f;  // the timer's rate while the field moves; 0 at rest
}
/*
 * The clock that turns the host's transport into rings:
 * UVGround.beatDefaults.
 */
namespace beat
{
inline constexpr float downbeat = 1.0f;  // a bar's first beat: the field's full source strength
inline constexpr float beat = 0.55f;  // every other quarter note: about half (a plugin may tune it)
inline constexpr float fps = 30.0f;  // ticks per second: one tick's travel is how far past a beat a start may land and still ring it
inline constexpr float slack = 0.0625f;  // quarter notes: a position this far behind the last tick is jitter, not a jump
}
}

} // namespace uv::tok

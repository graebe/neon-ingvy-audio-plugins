#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/*
 * ULTRAVIOLET — the design system's tokens, transcribed once.
 *
 * Every colour, size, spacing step and text style in this plugin comes from
 * here, and every value here comes from the design system's tokens.json. The
 * point of the single file is re-syncing: when the system moves, the diff is
 * against this header rather than a hunt through drawing code for the grey
 * that got typed in by hand. A ctest target fails the build if a colour
 * literal appears in any other source file.
 *
 * The system in one line: ultraviolet light on black glass. `uv` is THE
 * SIGNAL -- spent ONLY where something is on or IS the current value: a lit
 * step, a knob's value arc, the playhead, focus. Everything at rest is a
 * shade of violet-black. If a screen has more signal than black, something is
 * wrong.
 *
 * Ultraviolet is the older Phosphor system with its hue swapped and nothing
 * else: every size, space, stroke and text style is identical, and the move
 * was fourteen colour values. That is worth knowing when re-syncing -- a
 * diff against this header should be colours or it is not the same kind of
 * change.
 */
namespace uv
{

/* ------------------------------------------------------------- colour --
 * One theme, "ultraviolet". Usage notes are the system's own, abbreviated.
 * amber and red are unchanged from the previous hue: a warning and an error
 * are not brand colours, and they have to read as foreign to the palette. */
namespace colour
{
const juce::Colour bg000       { 0xff060410 };  /* window ground */
const juce::Colour bg100       { 0xff0c0818 };  /* panel */
const juce::Colour bg200       { 0xff140e24 };  /* control well */
const juce::Colour bg300       { 0xff1d1533 };  /* raised well: hover, pressed */
const juce::Colour line100     { 0xff1d1533 };  /* hairline: dividers, well borders */
const juce::Colour line200     { 0xff3a2a66 };  /* rail: the unlit part of an arc */
const juce::Colour ink         { 0xfff3ecff };  /* values, readouts, titles */
const juce::Colour inkMuted    { 0xffb9a3e6 };  /* labels, units, the hint bar */
const juce::Colour inkDim      { 0xff7a5fb5 };  /* disabled text, inactive marks */
/*
 * THE SIGNAL IS TWO TOKENS, AND THAT IS THE WHOLE IDEA.
 *
 * A near-white signal was tried here once and looked grey: with the fill at
 * the top of the lightness range there was no violet left anywhere, and the
 * window lost its cast. The system's answer is to split the job -- `uv` is
 * the bright CORE of anything lit, and `uvDeep` is the saturated violet that
 * surrounds it. The colour lives in the halo.
 *
 * So `uvDeep` is NEVER a fill on its own. It is the glow around a lit
 * element (see glowLed / glowFocus) and the tint of the ground's noise, and
 * nothing else. Reaching for it as a fill is how the two-tone collapses back
 * into one flat hue.
 *
 * `uv` and `ink` are close in lightness by construction now, so nothing may
 * tell them apart by colour: the ring's playhead is a separate mark and the
 * envelope's phase points are drawn in onUv for that reason.
 */
const juce::Colour uv          { 0xffefe3ff };  /* THE SIGNAL's bright core. */
const juce::Colour uvDeep      { 0xffa259ff };  /* its halo -- NEVER a fill */
const juce::Colour uvGlow      { 0x80a259ff };  /* bloom only -- never a fill */
const juce::Colour onUv        { 0xff060410 };  /* text on a signal fill */
const juce::Colour amber       { 0xffffb000 };  /* armed / about to clip / unsaved */
const juce::Colour red         { 0xffff4d4d };  /* clipping, a failed action */
/*
 * THE GROUND'S TEXTURE, WHICH IS NOW THE SYSTEM'S OWN AND NOT AN EXCEPTION.
 *
 * It used to be a neutral grey added here by hand, with a note apologising
 * for being the one achromatic value in a one-hue system. The system has
 * since specified the ground itself: 1px dots of `bgDot` on a space-3 pitch,
 * under noise tinted with `uvDeep` at 5%. Both are real tokens now, the
 * apology is gone, and the ground has the same cast as everything on it.
 *
 * Window ground only -- never inside a panel or a well.
 */
const juce::Colour bgDot       { 0xff2a1e4a };
}

/* -------------------------------------------------------------- sizes --
 * "Controls have one size each. Do not scale controls to fill a window;
 * leave space instead." */
namespace size
{
constexpr int knob     = 48;   /* knob disc diameter */
constexpr int knobLg   = 64;   /* at most one per window */
constexpr int step     = 40;   /* sequencer step, square */
constexpr int controlH = 28;   /* buttons, selects, readouts */
constexpr int readout  = 64;   /* a readout's MINIMUM width -- the Knob card's
                                * floor, below which a value clips */
constexpr int rail     = 2;    /* knob arc and slider track stroke */
constexpr int hairline = 1;    /* every border */
}

/* ------------------------------------------------------------ spacing --
 * Everything sits on a 4px grid. */
namespace space
{
constexpr int s1 = 4;    /* rail to bed; LED to label */
constexpr int s2 = 8;    /* steps in a grid; label to control */
constexpr int s3 = 12;   /* padding inside buttons and selects */
constexpr int s4 = 16;   /* controls in a row; panel padding */
constexpr int s6 = 24;   /* between control groups */
constexpr int s8 = 32;   /* window padding */
}

/* -------------------------------------------------------------- shape --
 * radius-0 everywhere; radius-1 only on a slider thumb and an LED housing;
 * radius-full only on what physically turns. There is no radius token here
 * because a rectangle with no corner radius needs no number -- if you find
 * yourself reaching for one, the shape is wrong. */
namespace stroke
{
constexpr float hair = 1.0f;
constexpr float rail = 2.0f;
}

/* --------------------------------------------------------------- type --
 * One family, JetBrains Mono, bundled with the plugin (OFL-1.1; see
 * Resources/OFL.txt). Monospace by construction means tabular digits, so a
 * readout does not jitter while a knob turns.
 *
 * JUCE has no letter-spacing on a Font, and the system tracks three of the
 * six styles (title .08em, label .1em, hint .02em). Tracked text is drawn
 * glyph by glyph -- see drawTracked() -- rather than faked with a wider font.
 */
namespace font
{
/*
 * THE BUNDLED FACES, AND WHY THEY ARE NOT TWO GLOBALS.
 *
 * A file-scope Typeface::Ptr is destroyed at program exit, which is AFTER
 * JUCE has shut down -- the release then takes a lock that no longer exists
 * and the process aborts with "mutex lock failed". Held through a
 * SharedResourcePointer in the LookAndFeel instead, they are released when
 * the last editor closes, which is while JUCE is still up.
 */
struct Faces
{
    Faces();
    ~Faces();
    juce::Typeface::Ptr regular, medium;
};
/* The live Faces, or nullptr when no editor is open (the style functions then
 * fall back to the system's monospace, which is what a headless render or a
 * host probing fonts before opening a window gets). */
extern Faces* faces;

juce::Font readout();   /* 28px/32 500  -- the one big number, once per window */
juce::Font title();     /* 12px/16 500  .08em uppercase -- panel names */
juce::Font label();     /* 11px/14 500  .1em  uppercase -- parameter labels */
juce::Font value();     /* 13px/16 400  -- readouts under controls, list rows */
juce::Font button();    /* 12px/16 500  .04em */
juce::Font hint();      /* 10px/14 400  .02em */

/* Tracking, in ems, for the styles that carry it. */
constexpr float trackTitle = 0.08f;
constexpr float trackLabel = 0.10f;
constexpr float trackHint  = 0.02f;
}

/* Draw text with letter-spacing, which juce::Font does not carry. `tracking`
 * is in ems, as the system states it. Returns the width drawn. */
float drawTracked (juce::Graphics&, const juce::String&, const juce::Font&,
                   juce::Colour, juce::Point<float> origin, float tracking);
float trackedWidth (const juce::String&, const juce::Font&, float tracking);

/* The two shadows the system allows, and nothing else.
 *
 *   glow-led    0 0 6px uv, 0 0 2px uv  -- a lit LED, the playhead
 *   glow-focus  0 0 0 1px uv, 0 0 6px uv-glow -- keyboard focus
 *
 * Drawn as concentric strokes of falling alpha: a blur would need a render
 * pass per control and this is a halo six pixels wide. */
void glowLed   (juce::Graphics&, juce::Rectangle<float>, float cornerRadius = 0.0f);
void glowFocus (juce::Graphics&, juce::Rectangle<float>, float cornerRadius = 0.0f);

}

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/*
 * PHOSPHOR — the design system's tokens, transcribed once.
 *
 * Every colour, size, spacing step and text style in this plugin comes from
 * here, and every value here comes from the design system's tokens.json. The
 * point of the single file is re-syncing: when the system moves, the diff is
 * against this header rather than a hunt through drawing code for the grey
 * that got typed in by hand. A ctest target fails the build if a colour
 * literal appears in any other source file.
 *
 * The system in one line: green light on black glass. `phosphor` is spent
 * ONLY where something is on or IS the current value -- a lit step, a knob's
 * value arc, the playhead, focus. Everything at rest is a shade of
 * green-black. If a screen has more phosphor than black, something is wrong.
 */
namespace phosphor
{

/* ------------------------------------------------------------- colour --
 * One theme, "matrix". Usage notes are the system's own, abbreviated. */
namespace colour
{
const juce::Colour bg000       { 0xff050705 };  /* window ground */
const juce::Colour bg100       { 0xff0b100b };  /* panel */
const juce::Colour bg200       { 0xff121b12 };  /* control well */
const juce::Colour bg300       { 0xff1a261a };  /* raised well: hover, pressed */
const juce::Colour line100     { 0xff1a261a };  /* hairline: dividers, well borders */
const juce::Colour line200     { 0xff2d5a37 };  /* rail: the unlit part of an arc */
const juce::Colour ink         { 0xff9dffb0 };  /* values, readouts, titles */
const juce::Colour inkMuted    { 0xff5fb571 };  /* labels, units, the hint bar */
const juce::Colour inkDim      { 0xff3d7a4b };  /* disabled text, inactive marks */
const juce::Colour phosphor    { 0xff00ff41 };  /* THE SIGNAL. On, and only on. */
const juce::Colour phosphorGlow{ 0x3300ff41 };  /* bloom only -- never a fill */
const juce::Colour onPhosphor  { 0xff050705 };  /* text on a phosphor fill */
const juce::Colour amber       { 0xffffb000 };  /* armed / about to clip / unsaved */
const juce::Colour red         { 0xffff4d4d };  /* clipping, a failed action */
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
 *   glow-led    0 0 6px phosphor, 0 0 2px phosphor  -- a lit LED, the playhead
 *   glow-focus  0 0 0 1px phosphor, 0 0 6px phosphor-glow -- keyboard focus
 *
 * Drawn as concentric strokes of falling alpha: a blur would need a render
 * pass per control and this is a halo six pixels wide. */
void glowLed   (juce::Graphics&, juce::Rectangle<float>, float cornerRadius = 0.0f);
void glowFocus (juce::Graphics&, juce::Rectangle<float>, float cornerRadius = 0.0f);

}

/*
 * @ultraviolet/ui -- the Ultraviolet design system, as Solid components.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * THREE INDEPENDENT THINGS an editor in this repository needs:
 *
 *   tokens.css      every colour, space, size and glow the system defines
 *   components.css  what those components look like (never where they sit)
 *   this file        the components, and the iPlug2 bridge
 *
 * THE COMPONENTS KNOW NOTHING ABOUT PARAMETERS. A Knob takes a normalised
 * number and reports one back; a Select takes an index. What the number MEANS
 * -- which host parameter, what units, whether it is an enum -- is the
 * caller's, and each editor wraps these with its own binding. That is what
 * lets the same Select serve the Trance Gate, which is bound to a parameter,
 * and the Spectrogram, which has none.
 *
 * The bridge is here for the opposite reason: it is a property of iPlug2 and
 * identical for every plugin, so it is shared exactly as it is.
 *
 * NOT HERE, DELIBERATELY:
 *
 *   Ring, StepGrid          one plugin's domain. The design system names both
 *                           because it was drawn FROM that plugin, but a
 *                           component with one caller has no API yet -- only a
 *                           shape. They move the day a second editor wants one,
 *                           which is the rule that brought the bridge and the
 *                           tokens here.
 *   Curve, EnvelopePlot,    the step-shaped half of the Trance Gate's Plots.
 *   PatternPlot,            Each of these knows what a STEP is, and a gate's
 *   StepRules, StepNumbers  envelope is not a ducker's -- two editors drawing a
 *                           curve is not two editors drawing the same curve.
 *   Slider                  the system names it; nothing uses one.
 *
 * WHAT THE RULE ABOVE BROUGHT IN IMMEDIATELY: Ground. It arrived with four
 * callers rather than one -- the design system gives EVERY window the same
 * animated ground -- so there was never a version of it that belonged to a
 * plugin. Its simulation is in lib/field.js, ported from the design system's own
 * reference implementation; the detector that drives it could not come with it
 * and lives in Rust (engines/ground), for the reason that file's header gives.
 *
 * WHAT THE RULE ABOVE HAS ALREADY MOVED: Well, Axis and band, when NI Side-Chain
 * became a second editor that needed a framed plot, a millisecond ruler and a
 * min/max waveform band. They are in components/Plot.jsx, and that file records
 * which half of Plots.jsx stayed behind.
 */
export { Knob } from './components/Knob.jsx';
/* A value edited in place: focused on insertion, Escape abandons it. */
export { EditField } from './components/EditField.jsx';
export { createTextEdit } from './lib/edit.js';
export { Toggle } from './components/Toggle.jsx';
export { Button } from './components/Button.jsx';
export { Select } from './components/Select.jsx';
/* Select chooses ONE and is a native <select> for the reasons in its header.
 * This chooses several, and is the design system's own open-list shape. */
export { CheckList } from './components/CheckList.jsx';
export { Tabs } from './components/Tabs.jsx';
export { Hint } from './components/Hint.jsx';
/* The window's ground. One per window, first child of it, and the only thing in
 * the design system that animates. */
export { Ground } from './components/Ground.jsx';
/* The window itself: ground, content from the top, the Hint bar at the bottom
 * edge, the fit-to-viewport scale and the height it reports. Every editor is
 * drawn inside one. */
export { EditorFrame, useFrame } from './components/EditorFrame.jsx';
export { createFit, fitScale, scaledHeight, reportHeight } from './lib/fit.js';
/* The playhead's clock: the engine's position carried forward per frame, and
 * no frame loop while nothing moves. */
export { createClock, positionAt } from './lib/clock.js';
/* The ready handshake and the ground's kicks, once for every editor. */
export { useEditorBridge, parseGround } from './lib/bridge.js';
/* Its switch's state, remembered per editor -- see the file for why this is not
 * a host parameter. */
export { createMotion } from './lib/motion.js';
/* Rendered by Hint, and exported so a window that somehow has no hint bar can
 * still carry the signature the design system requires of every window. */
export { Signature } from './components/Signature.jsx';

/* A scrolling spectrogram canvas. It takes finished columns and draws them --
 * the analysis, the wire format and what a column MEANS are the caller's, the
 * same seam every control here follows. */
export { default as Spectrogram } from './components/Spectrogram.jsx';
export { Well, Axis, band, INSET, CAPTION } from './components/Plot.jsx';
export { buildLut, readStops, readRgb, stopCss, luminance, STOPS, LEVELS } from './lib/ramp.js';

export { startDrag } from './lib/drag.js';
/* What a key does to a grid, a pad, a slider, a count or a tab strip. */
export { gridMove, padKey, sliderKey, tabMove, countKey } from './lib/keys.js';
/* The latest value per key, sent once a frame: for drags that message. */
export { createCoalescer } from './lib/coalesce.js';
export {
  setParam, beginGesture, endGesture, sendMessage, onParam, onMessage, onBytes,
} from './lib/iplug.js';
/* The shell's tags, the same in every plugin. Also importable on its own as
 * '@ultraviolet/ui/shell', which is what the editors' msg.js tables use. */
export { SHELL_MSG } from './lib/shell.js';

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
 * WHAT THE RULE ABOVE HAS ALREADY MOVED: Well, Axis and band, when NI Pump
 * became a second editor that needed a framed plot, a millisecond ruler and a
 * min/max waveform band. They are in components/Plot.jsx, and that file records
 * which half of Plots.jsx stayed behind.
 */
export { Knob } from './components/Knob.jsx';
export { Toggle } from './components/Toggle.jsx';
export { Button } from './components/Button.jsx';
export { Select } from './components/Select.jsx';
export { Tabs } from './components/Tabs.jsx';
export { Hint } from './components/Hint.jsx';

/* A scrolling spectrogram canvas. It takes finished columns and draws them --
 * the analysis, the wire format and what a column MEANS are the caller's, the
 * same seam every control here follows. */
export { default as Spectrogram } from './components/Spectrogram.jsx';
export { Well, Axis, band, INSET, CAPTION } from './components/Plot.jsx';
export { buildLut, readStops, stopCss, luminance, STOPS, LEVELS } from './lib/ramp.js';

export { startDrag } from './lib/drag.js';
export {
  setParam, beginGesture, endGesture, sendMessage, onParam, onMessage,
} from './lib/iplug.js';

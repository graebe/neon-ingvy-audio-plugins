/*
 * What every control in the editor does, in one line each.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The hint bar shows one of these while the pointer is over its control or the
 * keyboard focus is on it, and each is also the control's accessible
 * description (the kit's `info`). They are all here so they can be read,
 * reviewed and edited in one place, beside each other, in one voice.
 *
 * THE FORM: "Name — what it does." The name is drawn in `ink` and the rest in
 * `ink-muted`, the hint bar's pattern; the kit splits at the " — ". Plain,
 * specific and active, verb first where the control acts, and short enough for
 * the bar: aim for 70 characters, and tests/e2e holds every string to 80.
 *
 * Each is checked against what the control does (Params.cpp, steps.js, msg.js)
 * rather than what it is called -- a string that is wrong is worse than none.
 */

/* A knob's readout: the plugin parses what is typed, in the readout's units. */
const typed = (name) => `${name} value — click to type one; Enter sets it, Escape cancels.`;
/* The stages accept either unit, and the one typed wins over Time. */
const typedStage = (name) => `${name} value — click to type ms or %; the unit you type wins.`;

export const INFO = {
  /* The panels' titles, up their left edges. */
  gatePanel: 'Gate — when the steps fall and how much of each one sounds.',
  envelopePanel: 'Envelope — the shape every step is played with.',
  fadePanel: 'Fade — bring the pattern in a step at a time, in an order you set.',

  /* GATE */
  rate: 'Rate — the length of one step, synced to the song tempo.',
  length: 'Length — 1 to 128 steps; holds on bar lengths, Page Up/Down jumps.',
  amount: 'Amount — how deep the gate cuts, as dry/wet; 0 % bypasses it.',
  width: 'Width — how much of each step stays open before it releases.',

  /* ENVELOPE -- measured against the gate, in whichever unit Time asks for. */
  attack: 'Attack — how long a step takes to open, in ms or % of the gate.',
  decay: 'Decay — how long a step takes to fall from full to Sustain.',
  sustain: 'Sustain — the level a step holds after Decay, until it releases.',
  release: 'Release — how long a step takes to close once its Width ends.',

  /* FADE */
  fade: 'Fade — how much of the pattern has arrived; automate it for a build-up.',
  dir: 'Dir — In brings your steps in from silence; Out brings the holes in.',
  soft: 'Soft — ramp each arriving step in on its own level instead of jumping.',
  order: 'ORDER — tap the steps in the order the fade brings them; press to end.',
  shuffle: 'SHUFFLE — deal a random arrival order; the pattern stays as it is.',

  /* The settings row. */
  slot: 'Slot — pick one of 8 slots; each keeps its own pattern and sound.',
  join: 'Join Neighbors — run consecutive steps together instead of retriggering.',
  curve: 'Curve — the stages\' shape: Linear, Exponential or S-Curve.',
  time: 'Time — read Attack, Decay and Release in ms or as % of the gate.',
  random: 'RANDOM — fill this slot with a new pattern and arrival order.',
  copy: 'Copy slot — put this slot, pattern and sound, on the clipboard.',
  paste: 'Paste into slot — replace this slot from the clipboard; a bank, all 8.',

  /* The slot files, under the envelope plot. */
  exportSlot: 'EXPORT — save this slot to a .nitgslot file.',
  exportAll: 'EXPORT ALL — save all 8 slots to one .nitgbank file.',
  import: 'IMPORT — load a slot file into this slot, or a bank into all 8.',

  /* The ring, its count, and the plot under it. */
  ring: 'Ring — click a wedge to toggle its step, sweep to paint; arrows: Length.',
  steps: 'Steps — the pattern\'s Length; focus the ring and use the arrows.',
  envelopePlot: 'Envelope plot — one gate as the engine plays it, on a ms axis.',

  /* The band. */
  patternTab: 'Pattern — show one cycle of the gate as the engine renders it.',
  signalTab: 'Signal — show your audio before and after the gate, one cycle.',
  patternPlot: 'Pattern plot — one cycle of the gate, rendered by the engine.',
  signalPlot: 'Signal plot — the dry input in grey, the gated output in violet.',

  /* The pads, one string for all of them -- and another while ORDER is on,
   * because a click there names an arrival instead of toggling. */
  pads: 'Pads — click toggles, shift-click ties, drag sets amount; Space, Alt+↑↓.',
  padsOrder: 'Pads — click the steps in the order the fade should bring them in.',
  arrival: 'Arrival — click to type a new place; a number in use swaps.',

  /* The window's own: the switch and the signature in the hint bar. */
  motion: 'Motion — let the music ripple the background; remembered on this Mac.',
  signature: 'Neon Ingvy — the publisher of this plugin.',

  /* Every knob's readout. */
  readout: {
    rate: 'Rate value — click to type a division, such as 1/16 or 1/8T.',
    length: typed('Length'),
    amount: typed('Amount'),
    width: typed('Width'),
    attack: typedStage('Attack'),
    decay: typedStage('Decay'),
    sustain: typed('Sustain'),
    release: typedStage('Release'),
    fade: typed('Fade'),
  },
};

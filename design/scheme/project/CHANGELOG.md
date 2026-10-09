# Changelog

All notable changes to Ultraviolet, newest first. Versions follow the rules under Versioning.

## 1.2.0 — 2026-10-09

A plot can show the signal a processor reacts to, behind everything else: the kick a ducker ducks against.

**Added**

- `plot-key` (#ffb000): the reference a processor reacts to, drawn as a band behind the dry input — filled at 30% opacity under it, its 1px edge at 90% over it and under the processed trace (`ph-plot .key`, `.key-edge`). Drawn to its own height: it says when, not how loud. A data colour, only in a PlotWell.
- PlotWell: the duck example — the kick in amber, the input in grey, the ducked output in violet.

**Changed**

- `amber` is also a signal colour: its value is `plot-key`'s. A band inside a PlotWell is the reference signal; a rule or a mark is still the window's one warning, so the two never mean the same thing.

## 1.1.0 — 2026-10-06

The system now describes the plugins as they are built: the components the kit already ships, the ground on the host's beat, info on hover, knob detents and the window rules the editors keep.

**Added**

- Icons: `export` (save one item to a file), `export-all` (save every slot, a bank, to one file) and `import` (load from a file): one open tray drawn with copy and paste's stroke. Fifteen glyphs.
- Actions: the window verbs as one joined icon group, in order copy, paste, export, export all, import (the Trance Gate's slot verbs).
- Hint: info on hover and focus. Every control declares one line, at most 72 characters, written as *Name — what it does*; while the pointer is over the control or it has visible keyboard focus, the bar shows that line in place of the conventions, the name in `ink` and the rest in `ink-muted`, with a 150 ms grace on leaving. Precedence: an action's outcome, then hover info, then focus info, then the conventions. The line is also the control's accessible description. The Motion switch and the Signature never move, and Motion sits at least 16px before the Signature. Classes `tips stack`, `clauses`, `clauses held`, `info`.
- Knob: detents, values a drag holds on for about 14px of travel (Length at ½, 1, 2 and 4 bars for the current rate and meter); 1px radial ticks just outside the rail in `ink-dim`, the one at the current value `uv`; Shift ignores them, Page Up/Down jump between them. Classes `tick`, `tick on`.
- Interaction conventions: double-click resets to the parameter's default; a single click on a readout types into it, Enter commits and Escape abandons; the keyboard map (arrows step, Shift fine, Page Up/Down large steps or detents, Home/End the ends of the range); every pointer control reachable by Tab and operable from the keyboard.
- Panel: compact variant (`ph-panel compact`), the title up the left edge, 140px tall, for windows with three or more panels.
- Step: tie state (`ph-step tie`), a hollow `uv` outline with a bar across it.
- Ring: editable variant (`ph-ring edit`), wedges at least 24px deep; click toggles a step, a sweep paints, the arrow keys change Length.
- PlotWell (`ph-plot`): the frame of an engine-rendered plot, a `bg-000` well with a `line-100` hairline and a 10px caption.
- Meter (`ph-meter`): a level as a flat `uv` fill with its glow; no gradient, no transition, radius 0.
- TextField (`ph-field`): a 28px field for a name or a value; Enter keeps, Escape cancels.
- Data tokens: the spectrogram ramp `spec-0`…`spec-4`, and `plot-fill`, `plot-ghost` and `plot-dry` for plots, with the rule that data colours paint pictures of a signal and nothing else.
- Ground: `UVGround.BeatClock` (and `UVGround.beatDefaults`), a reference clock that turns the host's position, tempo, time signature and play state into rings.
- Spacing and layout: windows never scroll. Nothing is laid out outside the window, every list and menu opens inside it, and the window clips (`overflow: clip`).
- Motion: no editor behaviour depends on animation frames or page visibility.

**Changed**

- `on-uv` is #5d00d6, deep ultraviolet (`uv-deep`'s hue darkened, 7.0:1 on `uv`), was #060410: text and glyphs on a lit control are violet, not black. A tuned value; the meaning is unchanged.
- The record button's glyph on its red fill is `bg-000` (`.ph-btn.icon.rec.on`): `on-uv` on red would be 2.6:1.
- Ground: driven by the host's musical time, not by sound. One ring on every quarter note while the transport plays, a stronger one on each bar's downbeat from the host's time signature (strength 1.0 against about 0.5), nothing while it is stopped; identical in every plugin and independent of its audio. The window border and box edges still emit, and the wave physics are unchanged.
- Ground: the field runs on a timer at its frame rate, never on `requestAnimationFrame`, and is not paused by page visibility, because plugin hosts report their editor pages as hidden. It still stops at rest, and the host stops it when the window closes. The Motion switch sits in the Hint bar.
- Brand book: Colour, Spacing and layout, Motion, Interaction conventions and Iconography updated for the above; the Actions, Button, Ground, Hint, Icon, Knob (the bipolar arc stated in full), Panel, Readout, Ring and Step guidelines updated.
- The Icon, Hint and Knob previews show the new glyphs, the info state and detents with the bipolar arc, on the 1.1.0 stylesheet.

**Removed**

- The bass detector: `UVGround.BassDetector` and `UVGround.bassDefaults` (the 20–80 Hz onset detector), the detector row of the Motion table and the 20–80 Hz onset rule. No plugin called them: the plugins count the beat on their own side and hand the field each ring, so nothing built on 1.0.0 breaks.

## 1.0.0 — 2026-09-29

First release. Derived from Phosphor 1.0.0 with the signal hue moved to ultraviolet.

**Foundations**

- One theme, `ultraviolet`: grounds `bg-000`–`bg-300`, rails `line-100` / `line-200`, inks `ink` / `ink-muted` / `ink-dim`, signal `uv` (near-white violet) with `uv-deep` halo and `uv-glow`, text-on-signal `on-uv`, states `amber` / `red`, ground dot `bg-dot`.
- Type: JetBrains Mono only, six styles (`readout`, `title`, `label`, `value`, `button`, `hint`), tabular digits.
- 4px spacing scale, radii 0 / 2px / full, fixed control sizes, 1px / 2px strokes, `glow-led` and `glow-focus` as the only shadows.
- Window ground: dot paper at a 12px pitch with 5 % uniform `uv-deep` grain.

**Components**

- Controls: Knob, Slider, Button (labelled, icon, primary, transport group), Actions (labelled stack or joined icons), Select, Toggle (LED and switch), Readout, Icon.
- Sequencer: Step (off, on, partial amount filled from the bottom, play, accent), StepGrid, Ring.
- Layout: Panel, Hint, Signature, Ground.

**Ground (motion)**

- Bass-triggered wave field: damped 2D wave equation with sources at box edges and the window border, reflections and interference, ~20 s ring-out; 20–80 Hz onset detector; Motion switch and `prefers-reduced-motion` support. Reference implementation `components/ground.js` (`UVGround`).

**Assets**

- Icons: 12 machine glyphs (play, pause, stop, record, loop, copy, paste, shuffle, reset, link, chevron, power).
- Logos: `neon-ingvy-signature.svg`.

**Branding**

- Publisher Neon Ingvy; signature once per window at the right end of the Hint bar, and on the cover.

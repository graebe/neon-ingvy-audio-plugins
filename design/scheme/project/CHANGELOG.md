# Changelog

All notable changes to Ultraviolet, newest first. Versions follow the rules under Versioning.

## 2.4.0 — 2026-10-21

A radial menu can leave the object it opened on in sight.

**Added**

- RadialMenu: see-through — no plate and no centre text, a clear band through the centre that no item or label enters, items on two arcs above and below it, the hot item's value in its label, and only the items and labels taking presses.
- Interaction conventions: the Radial menu rule names the see-through menu.

## 2.3.0 — 2026-10-21

The object under a radial menu stays movable, and the menu is one keyboard stop.

**Added**

- RadialMenu: the middle may pass presses through (inside the items' inner edge, 36px): a press, a drag or a double-click there goes to the object under the menu, so it can be moved with its controls open.

**Changed**

- RadialMenu: one Tab stop. Tab into it takes its first item, or the one last used; Shift+Tab leaves the menu instead of going round backwards.
- Interaction conventions: the Radial menu rule names the pass-through middle.

## 2.2.0 — 2026-10-21

A well full of small objects edits them where they are: a click opens a circle of the object's controls around it, and a pitch view gets a piano down its left edge. Additions only; no token, class or rule changes.

**Added**

- RadialMenu: a circle of up to eight value and action items on a `bg-100` plate around the click, value items as knob dials (bipolar, detents), labels on chips outside the plate, the centre naming what it acts on. Guidelines and preview.
- PianoAxis: a vertical, display-only keyboard 32px wide that follows a pitch view's axis, lit keys in `uv`, the scale's keys marked in the gutter, each C named. Guidelines and preview.
- Interaction conventions: **Radial menu** — value items drag as knobs, actions act on a click, a press outside closes it and still acts, Shift may be a second parameter when the hint says so, and the keyboard map.

## 2.1.0 — 2026-10-18

Sliders are clicked where they should be; knobs are pulled. The Drag convention named both as dragged vertically by distance, while every slider built on the system already put its value where it was pressed. The owner's rule now reads as the controls work: a new rule that existing plugins already satisfy.

**Changed**

- Interaction conventions: **Drag** is the knobs' rule (click and pull up or down, 200 px for the range, Shift five times finer). New: **Click a slider where you want it** — a press puts the value at the pointer, a drag follows the rail, Shift from the press is finer from the value. New: **One edit per gesture**, the host-edit rule both share (opened on the first move, or on a press that changed the value).
- Slider: its guidelines state the pointer rule.
- The stylesheet's rules are unchanged; only its header names 2.1.0. The previews keep their 2.0.0 copy, which draws the same.

## 2.0.0 — 2026-10-12

The ground stands still. The animated window ground and its Motion switch are gone; the window ground is the static dot paper and grain it always was at rest, so nothing in a window moves on its own except a playhead. A major release: a component, its script and its classes are removed.

**Changed**

- Hint: the bar is the conventions, then the Signature at the right end, at least 16px (`space-4`) after the tips. The Signature still never moves.
- Motion: controls never animate and the window ground is still. The Timing rule keeps its general form: no editor behaviour depends on animation frames or page visibility, and a playhead runs on a timer.
- Brand book: Versioning, Spacing and layout and Motion updated for the above; the Hint guidelines and preview follow. The previews carry the 2.0.0 stylesheet.

**Removed**

- Ground: the component, its guidelines and preview, and `components/ground.js` (`UVGround.Field`, `UVGround.BeatClock`, `UVGround.defaults`, `UVGround.beatDefaults`, `UVGround.version`).
- Classes: `.ph-window.has-ground` and `.ph-ground`, and `.ph-hint .ph-toggle`, which existed only for the Motion switch.
- Hint: the Motion switch and its rules (its place before the Signature, its gap, its accessible description).
- Motion: the ground rules (the beat clock, the rings, the wave equation and its parameter table, the Motion switch and `prefers-reduced-motion`).
- `bg-dot`: the wave-peak clause. The dot is still the dot of the window ground.

## 1.3.0 — 2026-10-11

The ground looks like water: every ring is a drop at a point, so it spreads as a circle, bends around the boxes, echoes between them and fades slowly, where 1.2.0's rings rose from whole edges at once and travelled as straight, mechanical bands.

**Changed**

- Ground: a ring is a drop at one emitter, a Gaussian (σ 16 px) driven by the Ricker wavelet, not every box edge and the whole window border at once. Emitters stand just off the boxes' outer corners where open ground surrounds them; the downbeat falls at the anchor, the emitter nearest the window centre, and the bar's other beats at the next emitters clockwise. Each ring varies a little in frequency (± 8 %) and strength (± 12 %), the same way every time.
- Ground: the window border no longer emits, and reflects half of a wave meeting it head on (an impedance edge, `borderR`); box edges still reflect totally, so rings echo between the boxes.
- Ground: a viscous term ν·∇²u_t (`visc`, 22 px²/s) fades a wave by its wavenumber squared, so grid ripple dies at once and a ring softens as it goes; τ is 10 s and the field comes to rest about ten seconds after the last ring. `gain` is 220.
- Ground: ∇² is the isotropic 9-point stencil, so a ring stays round on the square grid.
- Ground: a dot shows the height plus the slope towards a light at the top left (`slope`, 0.7), so a crest lights on one side and shades on the other. At rest the ground is still exactly the static design.
- Ground: `trigger(strength, at)` takes an optional point; the preview drops a ring where it is clicked.
- Brand book: Motion describes the drops, the emitters, the border, the damping and the light; the Ground guidelines follow.

**Removed**

- Ground: the `border` option (the window border emitting).

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

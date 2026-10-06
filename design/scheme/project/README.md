A design language for Ableton Live plugins (Max for Live and VST), the sister of *Phosphor*: same machine, same rules, same components, lit in ultraviolet instead of green: every lit element is a near-white core (`uv`) with a violet halo (`uv-deep`), on a flat black ground textured with dot paper and a trace of violet noise. Every plugin built on it looks like the same machine: ultraviolet light on black glass, a fixed set of controls, nothing decorative.

## Branding

The publisher is **Neon Ingvy**. Every plugin window carries the publisher's `Signature` exactly once, at the right end of the `Hint` bar: a 6px square lit in `uv`, then NEON INGVY in 10px tracked capitals in `ink-muted`. The signature is the only branding inside a plugin: no logo in the control area, no name on panels, no splash. The plugin's own name is the host's (Live shows it in the device title bar), so the window does not repeat it. Outside a plugin, use the signature file under Logos.

## Versioning

Ultraviolet follows semantic versioning, MAJOR.MINOR.PATCH. The current release is **1.1.0**; every release is listed in the Changelog. The version is recorded in `tokens.json` (`meta.release`) and in the header of `components/bundle.css`, and exposed as `UVGround.version` by `components/ground.js`. A plugin states the Ultraviolet version it was built against in its own release notes.

- **MAJOR** — a change that breaks a plugin built on the previous release: a token, class, component, icon or API member renamed or removed, a token's meaning changed (e.g. `uv` used for something other than "on"), a layout rule reversed.
- **MINOR** — an addition that breaks nothing: a new token, component, variant, icon or API option; a new rule that existing plugins already satisfy.
- **PATCH** — a correction that keeps every name and meaning: a value tuned (a colour, a spacing, an animation parameter within its documented role), a rendering bug fixed, documentation clarified.

Every change to the system bumps the version, adds a Changelog entry dated and grouped as Added / Changed / Fixed / Removed, and updates both places the version is recorded. Nothing is changed in place under an existing version number.

## Principles

1. **A machine, not a picture of one.** Every parameter is a physical control: a knob, a slider, a step, a switch. Draw the control's *function* (its range, its value, whether it is on), never its material. No bevels, no brushed metal, no drop shadows, no gradients. The only depth is a 1px hairline (`line-100`) and the only light is `uv` with its `uv-deep` halo.
2. **Violet means on.** `uv` is the single signal colour. It is spent only where something *is* on or *is* the current value: a lit step, the value arc of a knob, the playhead, an LED, keyboard focus. Everything that is off, at rest or merely a label is a shade of violet-black (`bg-*`, `line-*`, `ink-*`). If a screen has more `uv` than black, something is wrong.
3. **Underground, not neon.** The style is a darkroom, not a nightclub: one hue (violet), one monospace face, square corners, a 4px grid. Restraint is the aesthetic. No scanlines, no glitch, no rain-of-glyphs.
4. **Same knob everywhere.** Controls have one size each (`knob` 48, `step` 40, `control-h` 28). Do not scale controls to fill a window; leave space instead. The one exception is a single `knob-lg` (64) per window for the parameter the plugin is about.
5. **Kilohearts density.** Windows are compact and single-purpose like the kHs effects in the Live browser: one row of knobs, a readout under each, a hint line at the bottom, done.

## Colour

Set the window ground in `bg-000` and each group of controls in a `bg-100` panel with a `line-100` hairline. Control wells (knob disc, slider bed, select field, a step that is off) are `bg-200`; a well under the pointer or a pressed button rises to `bg-300`. Rails that show a range without a value (the unlit part of a knob arc, a slider track) are `line-200`.

Text: `ink` for values, readouts and titles; `ink-muted` for labels, units and the hint bar; `ink-dim` only for disabled text and inactive marks (ring segments, tick marks). Both `ink` and `ink-muted` read on every `bg-*` at 6:1 or better. `ink-dim` reaches about 3:1 on `bg-300`, so it is never used for running text — labels of a disabled control are 11px bold uppercase and count as marks.

`uv` (#efe3ff) is the value colour (Principle 2): near-white with a violet cast, 17:1 on every ground. Text and glyphs on a `uv` fill are `on-uv` (#5d00d6), deep ultraviolet: `uv-deep`'s hue darkened until it reads at 7:1 on `uv`, so a lit button keeps its violet in its own letters. The violet itself is `uv-deep` (#a259ff) and appears only as light around `uv`: `glow-led` on every lit element, a drop-shadow on value arcs and slider fills. The window ground is `bg-000` with two textures: a dot-paper pattern (`bg-dot` 1px dots at a 12px pitch, offset half a pitch) and a 5% `uv-deep` noise over it, both tiled in CSS (`bundle.css` `.ph-window`). The noise is uniform white noise (every pixel an independent draw, 0–10 % opacity, mean 5 %), not fractal: it must look even at every scale. Panels and wells are flat `bg-*` fills, so controls always sit on an even surface. No glow lives in the background: light only ever surrounds an element. `uv-glow` is only ever a box-shadow halo (`glow-led`, `glow-focus`), never a fill.

`amber` and `red` are the only other hues and they are states, not decoration: `amber` for armed / about to clip / unsaved, `red` for clipping or a failed action. At most one amber mark per window; a red one should be rare enough to be alarming. The record button is the one control lit in `red`; its glyph there is `bg-000`, because `on-uv` on red falls under 3:1.

Data colours paint pictures of a signal and nothing else: never a control, a panel or text. A spectrogram maps magnitude onto the five stops `spec-0`…`spec-4`, sampled between them: `spec-0` is the value of `bg-000`, so silence is the window's own ground, and `spec-4` is the value of `uv`, so the loudest band is lit in the system's own signal. Inside a `PlotWell` a curve is a 2px `uv` line with its arc glow, the area under it `plot-fill` (the value of `bg-300`), a reference curve behind it (the envelope as dialled, behind what the gate leaves of it) `plot-ghost` (the value of `line-200`), and the dry input behind a processed trace `plot-dry` (#6b6b78) at half opacity. `plot-dry` is the one grey in the system, deliberately achromatic so it reads as "not the signal": the violet is what the plugin did, the grey is what came in.

## Type

One family, `mono` (JetBrains Mono, OFL; bundle the font with the plugin, previews load it from Google Fonts). Digits are tabular so a readout does not jitter while a knob turns.

- `readout` (28px) — the one number a plugin is about (step count, tempo). At most one per window.
- `title` (12px, uppercase, tracked) — the plugin or panel name, `ink-muted`.
- `label` (11px, uppercase, tracked) — parameter labels above controls, `ink-muted`, centred over the control.
- `value` (13px) — readouts under controls, select text, list rows, `ink`.
- `button` (12px, medium) — button text.
- `hint` (10px) — the hint bar, `ink-muted`, with the verb in `ink`; also a plot's caption.

Labels are the parameter's name in one word where possible (`ATTACK`, `RATE`, `AMOUNT`), uppercase via CSS, never abbreviated below three letters. Values carry their unit in `ink-muted` after the number: `32 ms`, `1/64`, `54 %`; unitless values show two decimals (`1.00`).

## Spacing and layout

Everything sits on a 4px grid. Window padding `space-8`; panel padding `space-4`; controls in a row `space-4` apart; control groups `space-6` apart; a label sits `space-2` above its control and the readout `space-2` below; steps in a grid are `space-2` apart.

A standard plugin window, top to bottom: an optional big `readout` or ring on the left; the control block (knobs in rows of up to six, grouped in panels by function; a window with three or more panels may stack them as compact panels, `Panel`); the sequencer or other wide component full-width; the `Hint` bar pinned to the bottom edge, carrying the conventions, the Motion switch and the Signature. Window verbs (copy, paste, export, export all, import) live in the side column right of the panels (`Actions`): at the top, flush with the panels' top edge, either as an equal-width labelled stack or as a joined icon group; the transport and Sync LED sit at the bottom, flush with the panels' bottom edge. Never among the knobs. Nothing is centred vertically: windows are laid out from the top, spare room stays at the bottom above the hint.

**Windows never scroll.** A plugin window has a fixed size and no scrollbar, so nothing is laid out outside it: every control is inside the window, and every list or menu opens inside it, never past its edge. A window whose content does not fit is redesigned, not scrolled. The window clips (`overflow: clip`): `clip` is not a scroll container, so focusing a control near an edge moves nothing, where `hidden` still scrolls to whatever takes focus and slides the whole window sideways.

## Shape, borders and light

`radius-0` everywhere, `radius-1` only on a slider thumb and an LED housing, `radius-full` only on what physically turns (knob discs, the ring). Borders are `stroke-hair` (1px) and rails `stroke-rail` (2px); nothing is thicker. No box shadows except `glow-led` on every lit element (LED, lit step, playhead, primary button) and `glow-focus` on the focused control.

## States

Every control has the same five states and shows them the same way: **off/rest** (well `bg-200`, rail `line-200`); **on/value** (`uv`); **hover** (well rises to `bg-300`, border `ink-dim`); **active/pressed** (fill `uv`, text `on-uv`); **disabled** (everything drops to `ink-dim` / `line-100`, no fill). Keyboard focus is `glow-focus` on any of them. There is no separate "selected" colour: selected is `uv`.

## Motion

Controls never animate: values change instantly, the playhead advances in hard steps, an LED switches without a fade, a meter jumps to its level, and nothing moves on preset load.

The one animation is the ground (`Ground`), and it keeps the host's musical time. While the transport plays, every quarter note makes every box edge and the window border emit one slow ring, and each bar's downbeat emits a stronger one; while the transport is stopped, nothing rings. A beat's ring is about half as strong as a downbeat's, so the bar reads in the background as one pulse and its lighter beats. The bar comes from the host's time signature: *numerator × 4 ÷ denominator* quarter notes, 4/4 when the host gives none, so in 7/8 the downbeat falls between two quarters and rings there. Starting on a beat rings it, starting between two waits for the next, and a loop or a jump never rings the beats it skipped, only the one it lands on. The ground reads no audio: it is identical in every plugin, on a drum bus or a silent return, and independent of what the plugin does to the sound.

The rings travel through the background only; panels, wells and the step grid are solid to them. They reflect off box edges and off the window border, cross each other and interfere, and the whole field, reflections included, rings out over about 20 seconds. On a peak the dots grow and brighten and the grain thickens; in a valley the dots shrink and dim and the grain thins. The grain itself never moves (moving grain reads as TV static), only its local density changes. With the transport stopped the ground settles back to exactly the static design and its timer stops.

The field is the damped 2D wave equation, u_tt = c²∇²u − γ·u_t + A·s·ψ(t − t₀) at the sources, simulated on a 6 px grid (every second node is a dot) with reflecting (Neumann) edges; ψ is a Ricker wavelet, s the ring's strength. Display is tanh(u).

| parameter | value | why |
| --- | --- | --- |
| clock | one ring per quarter note while the transport plays, a downbeat every numerator × 4 ÷ denominator quarters, nothing while stopped | the host's musical time, never the audio, so every plugin rings alike |
| ring strength s | 1.0 on a downbeat, about 0.5 on every other beat | the bar reads as one strong pulse and its lighter beats; a plugin may tune the lesser one |
| wave speed c | 100 px/s | slow |
| source wavelet f₀ | 1.5 Hz (wavelength ≈ 67 px) | a ring on every beat at 90–140 BPM keeps a field of similar strength instead of cancelling itself |
| damping | amplitude e-folding τ = 3.5 s (γ = 2/τ) | reflections run for ~20 s before the field is at rest |
| sources | every open grid node next to a box edge, plus the window border | |
| boundaries | box edges and window border reflect, no phase flip | |
| source strength A | 65 | one downbeat peaks at u ≈ 0.9; a playing transport holds about 0.4–0.8 |
| grid / step | 6 px, 1/60 s fixed, ≤ 6 steps per frame | Courant number 0.28, stable; ~0.1 ms per step |
| dot radius | 1.0 ± 0.4 px | with opacity ± 30 %, so 1x displays do not flicker |
| grain | 5 % base, 2–9 % range | uniform white noise in `uv-deep`, static tile |
| frame rate | 30 fps on a timer, 0 at rest | never on animation frames, never paused by page visibility |

Every window with a Ground has a Motion switch, in its `Hint` bar; `prefers-reduced-motion` disables the ground outright. A still frame of the ground, at any moment, must look like the static design.

**Timing.** A plugin editor is a web page inside its host, and hosts report that page as hidden while it is on screen and all but stop its animation frames. So no editor behaviour depends on animation frames or on page visibility: the ground, a playhead and anything else that moves on its own run on a timer at their own rate, and nothing pauses because the page says it is hidden. Work still stops when there is nothing to show (the field at rest, the transport stopped), and the host stops it when the window closes.

## Interaction conventions

Every control answers the pointer and the keyboard the same way, in every window.

- **Drag.** Knobs and sliders drag vertically: 200 px of travel covers the range, and Shift-drag is five times finer. A drag is one edit to the host, opened on the first move, so a click that moves nothing writes no automation. A knob with detents holds on each for about 14 px of travel (`Knob`).
- **Double-click** resets a control to its parameter's default: the plugin's default, not the bottom of the range.
- **Click a readout to type.** One click turns the readout into a field with the value selected; Enter (or a click elsewhere) keeps what was typed, Escape abandons it. The plugin parses the text in the readout's own units ("40 ms", "1/8T"); the interface never guesses a unit.
- **Steps.** Click to toggle, Shift-click for a tie, drag up or down for the step's amount. A click is not a drag (4 px of dead zone first), dragging to the floor switches the step off, and a step switched on comes back at its full amount. On an editable `Ring` a sweep paints the state of the first wedge across every wedge it crosses.
- **Keyboard.** Every pointer control is reachable by Tab and operable from the keyboard: a focus ring on a control you cannot operate is decoration. Arrows step (1 % of the range; Shift for fine, 0.2 %), Page Up and Page Down take large steps (10 %) or jump to the next detent, Home and End go to the ends of the range, and Enter on a knob types into its readout. Space or Enter presses a button or a switch. A step grid is one Tab stop: the arrows move between steps, Space or Enter toggles (Shift: a tie), and Alt with Up or Down sets the amount. Every keystroke is an edit of its own.
- **Say it once.** Every window states its conventions once, in the `Hint` bar, in the pattern *verb in `ink`, rest in `ink-muted`, separated by en dashes*. Every control states what it does in its info string (*Name — what it does*, at most 72 characters), which the bar shows while the control is pointed at or has visible keyboard focus (`Hint`).

## Iconography

One set of fifteen machine glyphs (`Icon`; files under Icons): play, pause, stop, record, loop, copy, paste, export, export all, import, shuffle, reset, link, chevron, power. 16px on a 28px control, 1.5px stroke, square caps, no rounded corners except on objects that are round; `play` and `record` are filled, the rest stroked. Icons are drawn in `currentColor`, so they take `ink` at rest, `on-uv` on a lit button, `bg-000` on the red record button and `ink-dim` when disabled.

Use an icon where it replaces a verb on something that acts: the transport (an icon-only `Button` per function, lit while active, record in `red`), the window verbs (copy, paste, export, export all, import: icon before the word, or the joined icon group in `Actions`), loop, link, reset, power, and the caret in a `Select`. The three file verbs share one open tray, drawn with copy and paste's stroke: an arrow up out of it saves the current item to a file (`export`), the same arrow over a doubled tray saves every slot, a bank, to one file (`export-all`), and an arrow down into it loads a file (`import`). Do not use icons beside parameter labels, for states an LED already shows, or as decoration; and never invent a glyph outside the set. A control that has no glyph in the set gets a word.

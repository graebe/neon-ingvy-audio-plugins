A design language for Ableton Live plugins (Max for Live and VST), the sister of *Phosphor*: same machine, same rules, same components, lit in ultraviolet instead of green: every lit element is a near-white core (`uv`) with a violet halo (`uv-deep`), on a flat black ground textured with dot paper and a trace of violet noise. Every plugin built on it looks like the same machine: ultraviolet light on black glass, a fixed set of controls, nothing decorative.

## Branding

The publisher is **Neon Ingvy**. Every plugin window carries the publisher's `Signature` exactly once, at the right end of the `Hint` bar: a 6px square lit in `uv`, then NEON INGVY in 10px tracked capitals in `ink-muted`. The signature is the only branding inside a plugin: no logo in the control area, no name on panels, no splash. The plugin's own name is the host's (Live shows it in the device title bar), so the window does not repeat it. Outside a plugin, use the signature file under Logos.

## Versioning

Ultraviolet follows semantic versioning, MAJOR.MINOR.PATCH. The current release is **1.0.0**; every release is listed in the Changelog. The version is recorded in `tokens.json` (`meta.release`) and in the header of `components/bundle.css`, and exposed as `UVGround.version` by `components/ground.js`. A plugin states the Ultraviolet version it was built against in its own release notes.

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

`uv` (#efe3ff) is the value colour (Principle 2): near-white with a violet cast, 17:1 on every ground, so `on-uv` text (black) on a `uv` fill is easy to read. The violet itself is `uv-deep` (#a259ff) and appears only as light around `uv`: `glow-led` on every lit element, a drop-shadow on value arcs and slider fills. The window ground is `bg-000` with two textures: a dot-paper pattern (`bg-dot` 1px dots at a 12px pitch, offset half a pitch) and a 5% `uv-deep` noise over it, both tiled in CSS (`bundle.css` `.ph-window`). The noise is uniform white noise (every pixel an independent draw, 0–10 % opacity, mean 5 %), not fractal: it must look even at every scale. Panels and wells are flat `bg-*` fills, so controls always sit on an even surface. No glow lives in the background: light only ever surrounds an element. Text on a `uv` fill is `on-uv`. `uv-glow` is only ever a box-shadow halo (`glow-led`, `glow-focus`), never a fill.

`amber` and `red` are the only other hues and they are states, not decoration: `amber` for armed / about to clip / unsaved, `red` for clipping or a failed action. At most one amber mark per window; a red one should be rare enough to be alarming.

## Type

One family, `mono` (JetBrains Mono, OFL; bundle the font with the plugin, previews load it from Google Fonts). Digits are tabular so a readout does not jitter while a knob turns.

- `readout` (28px) — the one number a plugin is about (step count, tempo). At most one per window.
- `title` (12px, uppercase, tracked) — the plugin or panel name, `ink-muted`.
- `label` (11px, uppercase, tracked) — parameter labels above controls, `ink-muted`, centred over the control.
- `value` (13px) — readouts under controls, select text, list rows, `ink`.
- `button` (12px, medium) — button text.
- `hint` (10px) — the hint bar, `ink-muted`, with the verb in `ink`.

Labels are the parameter's name in one word where possible (`ATTACK`, `RATE`, `AMOUNT`), uppercase via CSS, never abbreviated below three letters. Values carry their unit in `ink-muted` after the number: `32 ms`, `1/64`, `54 %`; unitless values show two decimals (`1.00`).

## Spacing and layout

Everything sits on a 4px grid. Window padding `space-8`; panel padding `space-4`; controls in a row `space-4` apart; control groups `space-6` apart; a label sits `space-2` above its control and the readout `space-2` below; steps in a grid are `space-2` apart.

A standard plugin window, top to bottom: an optional big `readout` or ring on the left; the control block (knobs in rows of up to six, grouped in panels by function); the sequencer or other wide component full-width; the `Hint` bar pinned to the bottom edge. Window verbs (`Copy patch`, `Paste patch`) live in the side column right of the panels (`Actions`): at the top, flush with the panels' top edge, either as an equal-width labelled stack or as a joined icon pair; the transport and Sync LED sit at the bottom, flush with the panels' bottom edge. Never among the knobs. Nothing is centred vertically: windows are laid out from the top, spare room stays at the bottom above the hint.

## Shape, borders and light

`radius-0` everywhere, `radius-1` only on a slider thumb and an LED housing, `radius-full` only on what physically turns (knob discs, the ring). Borders are `stroke-hair` (1px) and rails `stroke-rail` (2px); nothing is thicker. No box shadows except `glow-led` on every lit element (LED, lit step, playhead, primary button) and `glow-focus` on the focused control.

## States

Every control has the same five states and shows them the same way: **off/rest** (well `bg-200`, rail `line-200`); **on/value** (`uv`); **hover** (well rises to `bg-300`, border `ink-dim`); **active/pressed** (fill `uv`, text `on-uv`); **disabled** (everything drops to `ink-dim` / `line-100`, no fill). Keyboard focus is `glow-focus` on any of them. There is no separate "selected" colour: selected is `uv`.

## Motion

Controls never animate: values change instantly, the playhead advances in hard steps, an LED switches without a fade, and nothing moves on preset load.

The one animation is the ground (`Ground`), and it is driven by sound, not by time. A kick drum (onsets in 20–80 Hz) makes every box edge and the window border emit one slow ring. The rings travel through the background only; panels, wells and the step grid are solid to them. They reflect off box edges and off the window border, cross each other and interfere, and the whole field, reflections included, rings out over about 20 seconds. On a peak the dots grow and brighten and the grain thickens; in a valley the dots shrink and dim and the grain thins. The grain itself never moves (moving grain reads as TV static), only its local density changes. With no sound the ground settles back to exactly the static design and the render loop stops.

The field is the damped 2D wave equation, u_tt = c²∇²u − γ·u_t + A·s·ψ(t − t₀) at the sources, simulated on a 6 px grid (every second node is a dot) with reflecting (Neumann) edges; ψ is a Ricker wavelet, s the kick strength. Display is tanh(u).

| parameter | value | why |
| --- | --- | --- |
| wave speed c | 100 px/s | slow |
| source wavelet f₀ | 1.5 Hz (wavelength ≈ 67 px) | a steady kick at 90–140 BPM keeps a field of similar strength instead of cancelling itself |
| damping | amplitude e-folding τ = 3.5 s (γ = 2/τ) | reflections run for ~20 s before the field is at rest |
| sources | every open grid node next to a box edge, plus the window border | |
| boundaries | box edges and window border reflect, no phase flip | |
| source strength A | 65 | one kick peaks at u ≈ 0.9; continuous kicks hold 0.5–0.8 |
| grid / step | 6 px, 1/60 s fixed, ≤ 6 steps per frame | Courant number 0.28, stable; ~0.1 ms per step |
| dot radius | 1.0 ± 0.4 px | with opacity ± 30 %, so 1x displays do not flicker |
| grain | 5 % base, 2–9 % range | uniform white noise in `uv-deep`, static tile |
| detector | 20–80 Hz, 12 dB/oct each side, env 5 / 150 ms, onset at 1.8 × 300 ms mean, 120 ms refractory | strength √ratio, clamped 0.3–1 |
| frame rate | 30 fps, 0 at rest | |

Every window with a Ground has a Motion switch; `prefers-reduced-motion` disables it outright. A still frame of the ground, at any moment, must look like the static design.

## Interaction conventions

Knobs and sliders: drag vertically, shift-drag for fine, double-click to reset, click the readout to type. Steps: click to toggle, drag up/down for the step's amount. Every window states its own conventions once, in the `Hint` bar, in the pattern *verb in `ink`, rest in `ink-muted`, separated by en dashes*.

## Iconography

One set of twelve machine glyphs (`Icon`; files under Icons): play, pause, stop, record, loop, copy, paste, shuffle, reset, link, chevron, power. 16px on a 28px control, 1.5px stroke, square caps, no rounded corners except on objects that are round; `play` and `record` are filled, the rest stroked. Icons are drawn in `currentColor`, so they take `ink` at rest, `on-uv` on a lit button and `ink-dim` when disabled.

Use an icon where it replaces a verb on something that acts: the transport (an icon-only `Button` per function, lit while active, record in `red`), copy and paste (icon before the word), loop, link, reset, power, and the caret in a `Select`. Do not use icons beside parameter labels, for states an LED already shows, or as decoration; and never invent a glyph outside the set. A control that has no glyph in the set gets a word.

A design language for Ableton Live plugins (Max for Live and VST), the sister of *Phosphor*: same machine, same rules, same components, lit in ultraviolet instead of green: every lit element is a near-white core (`uv`) with a violet halo (`uv-deep`), on a flat black ground textured with dot paper and a trace of violet noise. Every plugin built on it looks like the same machine: ultraviolet light on black glass, a fixed set of controls, nothing decorative.

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

A standard plugin window, top to bottom: an optional big `readout` or ring on the left; the control block (knobs in rows of up to six, grouped in panels by function); the sequencer or other wide component full-width; the `Hint` bar pinned to the bottom edge. Actions (`Copy patch`, `Paste patch`) stack on the right edge of the control block, never among the knobs. Nothing is centred vertically: windows are laid out from the top, spare room stays at the bottom above the hint.

## Shape, borders and light

`radius-0` everywhere, `radius-1` only on a slider thumb and an LED housing, `radius-full` only on what physically turns (knob discs, the ring). Borders are `stroke-hair` (1px) and rails `stroke-rail` (2px); nothing is thicker. No box shadows except `glow-led` on every lit element (LED, lit step, playhead, primary button) and `glow-focus` on the focused control.

## States

Every control has the same five states and shows them the same way: **off/rest** (well `bg-200`, rail `line-200`); **on/value** (`uv`); **hover** (well rises to `bg-300`, border `ink-dim`); **active/pressed** (fill `uv`, text `on-uv`); **disabled** (everything drops to `ink-dim` / `line-100`, no fill). Keyboard focus is `glow-focus` on any of them. There is no separate "selected" colour: selected is `uv`.

## Motion

Controls never animate: values change instantly, the playhead advances in hard steps, an LED switches without a fade, and nothing moves on preset load.

The one animation is the ground (`Ground`), and it is driven by sound, not by time. A kick drum (onsets in 20–80 Hz) sends a wave out from the edges of every box in the window; the wave travels slowly through the background only, underneath panels, wells and the step grid, which occlude it. A wave is a short packet, w = s · cos(2π(d − c·t)/λ) · exp(−((d − c·t)/σ)²) · exp(−t/τ), where d is a dot's distance to the nearest box edge; overlapping waves add and are soft-clipped. On a peak the dots grow and brighten and the grain thickens; in a valley the dots shrink and dim and the grain thins. The grain itself never moves (moving grain reads as TV static), only its local density changes. With no sound the ground is perfectly still and the render loop stops.

| parameter | value | why |
| --- | --- | --- |
| wave speed c | 100 px/s | slow, yet kicks at 127 BPM stay distinguishable |
| packet width σ / wavelength λ | 48 px / 96 px | 4 and 8 dot pitches: a ring, not a pulse |
| decay τ | 2.5 s | a wave fades before it crosses the window |
| concurrent waves | 8 | the weakest fades out over 250 ms beyond that |
| dot radius | 1.0 ± 0.4 px | with opacity ± 30 %, so 1x displays do not flicker |
| grain | 5 % base, 2–9 % range | uniform white noise in `uv-deep`, static tile |
| detector | 20–80 Hz, 12 dB/oct each side, env 5 / 150 ms, onset at 1.8 × 300 ms mean, 120 ms refractory | strength √ratio, clamped 0.3–1: loud kicks are slightly more visible, never large |
| frame rate | 30 fps, 0 at idle | |

Every window with a Ground has a Motion switch; `prefers-reduced-motion` disables it outright. A still frame of the ground, at any moment, must look like the static design.

## Interaction conventions

Knobs and sliders: drag vertically, shift-drag for fine, double-click to reset, click the readout to type. Steps: click to toggle, drag up/down for the step's amount. Every window states its own conventions once, in the `Hint` bar, in the pattern *verb in `ink`, rest in `ink-muted`, separated by en dashes*.

## Iconography

One set of twelve machine glyphs (`Icon`; files under Icons): play, pause, stop, record, loop, copy, paste, shuffle, reset, link, chevron, power. 16px on a 28px control, 1.5px stroke, square caps, no rounded corners except on objects that are round; `play` and `record` are filled, the rest stroked. Icons are drawn in `currentColor`, so they take `ink` at rest, `on-uv` on a lit button and `ink-dim` when disabled.

Use an icon where it replaces a verb on something that acts: the transport (an icon-only `Button` per function, lit while active, record in `red`), copy and paste (icon before the word), loop, link, reset, power, and the caret in a `Select`. Do not use icons beside parameter labels, for states an LED already shows, or as decoration; and never invent a glyph outside the set. A control that has no glyph in the set gets a word.

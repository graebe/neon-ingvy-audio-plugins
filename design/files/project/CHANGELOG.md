# Changelog

All notable changes to Ultraviolet, newest first. Versions follow the rules under Versioning.

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

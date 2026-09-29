# Ground

The animated window ground: the dot paper and grain as the surface of a slow, damped wave field. A kick drum makes every box edge and the window border emit a ring; rings reflect off boxes and the border, interfere, and ring out over about 20 s (model and parameters: Motion in the brand book). Put a `canvas.ph-ground` as the first child of a `.ph-window.has-ground`; the window's other children stay above it. The static CSS ground stays underneath as the fallback and is pixel-identical to the canvas at rest.

**Consumer provides:** the canvas; the boxes that emit and reflect (by default every `.ph-panel`, `.ph-grid`, `.ph-ring` and `[data-wave-source]` in the window, as rects relative to the canvas, passed to `setSources` and recomputed on resize); an audio signal for `BassDetector` (the device input, or a sidechain from the master when the device sits on a non-bass track); and a Motion switch (`Toggle`) wired to `setEnabled`.

**API** (`components/ground.js`, global `UVGround`): `new Field(canvas, opts)`, `.setSources(rects)`, `.trigger(strength 0–1)`, `.setEnabled(bool)`, `.resize()`, `.destroy()`; `new BassDetector(audioContext, onOnset, opts)` with `.input` (connect audio here), `.start()`, `.stop()`; onsets call `onOnset(strength)`.

**Do:** feed it bass only, through the detector; keep one Ground per window; let the loop stop when the field is at rest. Tune strength with `gain`, persistence with `tau`, ring size with `freq`. **Don't:** add idle motion when nothing plays, run waves through panels or wells (they are opaque on purpose), raise `gain` or `tau` until the field reads as an effect (a long `tau` under a steady kick fills the window), or use it in a window without a Motion switch. `prefers-reduced-motion` turns it off regardless.

In the preview: click the ground for a kick, play for a synthesized 127 BPM kick through the real 20–80 Hz detector (silent), listen to use the microphone where the browser allows it.

# Toggle

Two forms. **LED**: an 8px `radius-full` lens, `ink-dim` off, `uv` with `glow-led` on; `amber` for armed, `red` for clip. Read-only status goes here. **Switch**: a 28×14 `bg-200` housing with a square that moves right and lights up. A user-settable boolean (loop, sync) is a switch; a state the plugin reports (clipping, MIDI in) is an LED. Both carry a `label` to the right. Consumer provides: label, on, kind, and for the LED a status (`on`, `warn`, `clip`).

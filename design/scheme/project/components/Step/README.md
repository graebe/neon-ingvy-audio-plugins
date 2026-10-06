# Step

One cell of a sequencer: a 40px square well. `off` is `bg-200`; `on` is a solid `uv` fill with `glow-led`; a partial amount (`on partial`) keeps the `bg-200` well, takes a `uv` border and fills with `uv` from the bottom, the lit height being the amount; `play` (the playhead) adds `glow-led` and, on an off step, a `uv-glow` wash; `accent` is an `amber` border. States combine (`on play`). Consumer provides: on, amount 0–1, playing, accent, an optional index for the first step of each beat. Click toggles, vertical drag sets the amount; state the conventions in the window's `Hint`.

# PianoAxis

A vertical piano keyboard on the left edge of a pitch view, 32px wide, its keys lined up with the view's pitch rows at every zoom and scroll, so a note's height reads as a key. It is display only, never a control.

White keys are `bg-200` wells and black keys `bg-000`, both on `line-100` hairlines, the black ones 60 % of the width; a key the window marks as lit (the selected or dragged object's pitch) fills `uv` with its glow. The keys of the current scale carry a 4px mark in the gutter beside them, `ink-dim`, the tonic's in `ink-muted`: marks, not colour, since amber is kept for the window's one warning. Each C is named in `hint` type (C3 = MIDI 60, as the pitch view names it). Rows smaller than 5px drop the marks and rows under 4px the gaps, so a zoomed-out axis stays a clean keyboard. Consumer provides: the pitch axis it follows (the same one the view draws with), the lit pitches, the scale and its tonic, and whether it is dimmed.

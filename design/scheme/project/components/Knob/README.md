# Knob

The default control for any continuous parameter. A `bg-200` disc, a 270° `line-200` rail, a `uv` value arc from the minimum and a 2px pointer; `label` above, `Readout` below. Consumer provides: label, value 0–1, formatted readout string, size (`knob` 48 default, `knob-lg` 64 at most once per window), disabled. Bipolar parameters (pan, detune) draw the arc from 12 o'clock, not from the minimum. Never draw a knob without its readout: the arc shows position, the readout shows the number.

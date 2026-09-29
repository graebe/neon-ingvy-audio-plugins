/*!
`chain_params` -- how Schwung's knob grid draws NI Side-Chain.

ONE STRING LITERAL, AND IT IS A CAPTURE RATHER THAN A DECLARATION. The shape is
the C shell's own output, and `engines/side-chain/tests/dump_params.c` is what keeps it
that way: the test prints what the module serves and compares it against this
file, so a parameter added to the engine and forgotten here is a test failure
rather than a control that quietly does not appear.

THE FOUR ENVELOPE KEYS ARE CONTIGUOUS ON PURPOSE. A viz group whose members are
not adjacent is reported by the host's detector as `viz-declared-not-adjacent`,
and the graphic is dropped.

SIDECHAIN IS NOT AN OPTION HERE. The chain host hands over one interleaved
buffer and the API has no aux input anywhere, so the Move build offers Cycle and
MIDI. Listing a third source that could never fire would be worse than listing
two: a control that does nothing reads as a broken module rather than as a
platform that lacks the bus.

THRESHOLD AND LOCKOUT ARE ABSENT FOR THE SAME REASON -- they are the sidechain
detector's, and the detector has nothing to listen to.
*/

pub const CHAIN_PARAMS: &str = r##"[{"key":"source","name":"Source","type":"enum","options":["Cycle","MIDI"],"wire_format":"index","default":"0"},{"key":"rate","name":"Rate","type":"enum","options":["1/1","1/1T","1/2","1/2T","1/4","1/4T","1/8","1/8T","1/16","1/16T","1/32","1/32T"],"default":"1/4"},{"key":"depth","name":"Depth","type":"float","min":0,"max":1,"default":1,"step":0.01,"unit":"%"},{"key":"delay","name":"Delay","short_name":"Dly","type":"float","min":0,"max":100,"default":0,"step":0.1,"unit":"%","viz":{"group":"duck","role":"delay","kind":"envelope"}},{"key":"attack","name":"Attack","short_name":"Att","type":"float","min":0,"max":200,"default":2,"step":0.1,"unit":"%","viz":{"group":"duck","role":"attack"}},{"key":"hold","name":"Hold","short_name":"Hold","type":"float","min":0,"max":200,"default":8,"step":0.1,"unit":"%","viz":{"group":"duck","role":"hold"}},{"key":"release","name":"Release","short_name":"Rel","type":"float","min":0,"max":200,"default":35,"step":0.1,"unit":"%","viz":{"group":"duck","role":"release"}},{"key":"curve","name":"Curve","type":"enum","options":["Linear","Exponential","S-Curve","Pump"],"wire_format":"index","default":"1"},{"key":"time_mode","name":"Time","type":"enum","options":["ms","% of cycle"],"wire_format":"index","default":"0"},{"key":"channel","name":"Channel","short_name":"Ch","type":"enum","options":["Omni","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"],"wire_format":"index","default":"1"},{"key":"trigger_note","name":"Trigger","type":"enum","options":["C-2","C#-2","D-2","D#-2","E-2","F-2","F#-2","G-2","G#-2","A-2","A#-2","B-2","C-1","C#-1","D-1","D#-1","E-1","F-1","F#-1","G-1","G#-1","A-1","A#-1","B-1","C0","C#0","D0","D#0","E0","F0","F#0","G0","G#0","A0","A#0","B0","C1","C#1","D1","D#1","E1","F1","F#1","G1","G#1","A1","A#1","B1","C2","C#2","D2","D#2","E2","F2","F#2","G2","G#2","A2","A#2","B2","C3","C#3","D3","D#3","E3","F3","F#3","G3","G#3","A3","A#3","B3","C4","C#4","D4","D#4","E4","F4","F#4","G4","G#4","A4","A#4","B4","C5","C#5","D5","D#5","E5","F5","F#5","G5","G#5","A5","A#5","B5","C6","C#6","D6","D#6","E6","F6","F#6","G6","G#6","A6","A#6","B6","C7","C#7","D7","D#7","E7","F7","F#7","G7","G#7","A7","A#7","B7","C8","C#8","D8","D#8","E8","F8","F#8","G8"],"default":"C1"},{"key":"midi_mode","name":"Mode","type":"enum","options":["Trigger","Gate"],"wire_format":"index","default":"0"},{"key":"vel_sens","name":"Vel Sens","short_name":"Vel","type":"float","min":0,"max":1,"default":0,"step":0.01,"unit":"%"},{"key":"ui","name":"UI","type":"string","access":"read"},{"key":"sweep","name":"Sweep","type":"string","access":"read","live":true},{"key":"shape","name":"Shape","type":"canvas","as_page":true,"show_value":false,"extra_keys":["ui","rate"]}]"##;

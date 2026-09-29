/*!
The `chain_params` contract the knob grid reads.

THIS copy is the one the UI reads -- the chain host asks the plugin first and
only falls back to re-parsing `module.json`, which drops `viz` entirely.
Declared here rather than in `module.json` for a second reason: that file is
read by a minimal parser with an 8 KB budget and this does not comfortably
fit.

CAPTURED FROM THE C SHELL'S OWN OUTPUT, not transcribed from its source. The
first attempt lifted the string literals out of the C shell's source by regex and
picked up the ones inside its comments as well -- 79 extra bytes and a stray
"1" in front of the opening bracket, which the UI would have read as a
malformed contract. `tests/dump_params.c` prints what the shell actually
serves, so that is what this is, byte for byte, and `smoke_ui.mjs` is what
keeps it honest.

The four envelope keys are CONTIGUOUS on purpose: a viz group whose members
do not land on one row of the 2x4 page is dropped whole, silently. The
reasoning behind each individual declaration stayed in the C shell's history,
where it belongs.

THE FADE'S THREE ARE APPENDED AFTER `amount`, not inserted anywhere nearer the
front, for two separate reasons: the adsr block above must not be split, and a
canvas page carries only the level's FIRST EIGHT knobs -- so anything added
ahead of `hold` would push a control off the ring page that is already there.

`fade_dir` chooses which end the pattern is built up from -- In introduces the
steps you drew on, Out introduces the holes -- and the knob means the same thing
in both, so 100% is the pattern either way and In is the inert default.

`randomize` IS AN ENUM AND NOT A TRIGGER because released Schwung has no
write-only param type. That is why it needs a "Hold": a knob writes whichever
option it is turned to, so without a value that does nothing, turning it back
would roll a second pattern. It writes the LABEL rather than an index
deliberately -- "Roll" carries no number, so the engine walks its own generator
and successive rolls differ, where index 1 would pin the seed to 1 and hand back
the same pattern every time.
*/

pub const CHAIN_PARAMS: &str = r##"[{"key":"slot","name":"Slot","type":"enum","wire_format":"index","options":["1","2","3","4","5","6","7","8"],"default":"0"},{"key":"step_amount","name":"Step Amount","short_name":"Step","type":"float","min":0,"max":1,"default":1,"step":0.01,"unit":"%"},{"key":"length","name":"Len","type":"enum","wire_format":"index","default":"15","options":["1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32","33","34","35","36","37","38","39","40","41","42","43","44","45","46","47","48","49","50","51","52","53","54","55","56","57","58","59","60","61","62","63","64","65","66","67","68","69","70","71","72","73","74","75","76","77","78","79","80","81","82","83","84","85","86","87","88","89","90","91","92","93","94","95","96","97","98","99","100","101","102","103","104","105","106","107","108","109","110","111","112","113","114","115","116","117","118","119","120","121","122","123","124","125","126","127","128"]},{"key":"rate","name":"Rate","type":"enum","options":["1/1T","1/2","1/2T","1/4","1/4T","1/8","1/8T","1/16","1/16T","1/32","1/32T","1/64","1/128"],"default":"1/16"},{"key":"cursor","name":"Step","type":"enum","wire_format":"index","default":"0","options":["1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32","33","34","35","36","37","38","39","40","41","42","43","44","45","46","47","48","49","50","51","52","53","54","55","56","57","58","59","60","61","62","63","64","65","66","67","68","69","70","71","72","73","74","75","76","77","78","79","80","81","82","83","84","85","86","87","88","89","90","91","92","93","94","95","96","97","98","99","100","101","102","103","104","105","106","107","108","109","110","111","112","113","114","115","116","117","118","119","120","121","122","123","124","125","126","127","128"]},{"key":"step","name":"Gate","short_name":"Gate","type":"enum","options":["Off","On","Tie"],"default":"Off"},{"key":"legato","name":"Join Neighbors","short_name":"Join","type":"enum","options":["Off","On"],"wire_format":"index","default":"0"},{"key":"time_mode","name":"Env Time","short_name":"Time","type":"enum","options":["ms","%"],"wire_format":"index","default":"0"},{"key":"curve","name":"Env Curve","short_name":"Curve","type":"enum","options":["Linear","Exponential","S-Curve"],"wire_format":"index","default":"0"},{"key":"attack","name":"Att","type":"float","min":0,"max":200,"default":1.6,"step":0.1,"unit":"%","viz":{"group":"adsr","role":"attack","kind":"envelope"}},{"key":"decay","name":"Dec","type":"float","min":0,"max":200,"default":16,"step":0.1,"unit":"%","viz":{"group":"adsr","role":"decay"}},{"key":"sustain","name":"Sus","type":"float","min":0,"max":1,"default":1,"step":0.01,"unit":"%","viz":{"group":"adsr","role":"sustain"}},{"key":"release","name":"Rel","type":"float","min":0,"max":200,"default":16,"step":0.1,"unit":"%","viz":{"group":"adsr","role":"release"}},{"key":"hold","name":"Width","short_name":"Width","type":"float","min":0.05,"max":1,"default":1,"step":0.01,"unit":"%"},{"key":"amount","name":"All Amount","short_name":"All","type":"float","min":0,"max":1,"default":1,"step":0.01,"unit":"%"},{"key":"fade","name":"Fade In","short_name":"Fade","type":"float","min":0,"max":1,"default":1,"step":0.01,"unit":"%"},{"key":"fade_soft","name":"Fade Shape","short_name":"Shape","type":"enum","options":["Hard","Soft"],"wire_format":"index","default":"0"},{"key":"fade_dir","name":"Fade Dir","short_name":"Dir","type":"enum","options":["In","Out"],"wire_format":"index","default":"0"},{"key":"randomize","name":"Random","short_name":"Rnd","type":"enum","options":["Hold","Roll"],"default":"Hold"},{"key":"pattern","name":"Pat","type":"string","access":"read"},{"key":"ties","name":"Ties","type":"string","access":"read"},{"key":"phase","name":"Phase","type":"string","access":"read","live":true},{"key":"ui","name":"UI","type":"string","access":"read"},{"key":"gate","name":"Gate","type":"canvas","as_page":true,"show_value":false,"extra_keys":["ui","rate"]}]"##;

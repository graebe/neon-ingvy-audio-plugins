// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The `chain_params` contract the knob grid reads.

THIS copy is the one the UI reads -- the chain host asks the plugin first and
only falls back to re-parsing `module.json`, which drops `viz` entirely.
Declared here rather than in `module.json` for a second reason: that file is
read by a minimal parser with an 8 KB budget and this does not comfortably
fit.

WRITTEN BY build.rs FROM THE ENGINE'S SCHEMA (tg_core::schema), so every
automatable parameter's range, default and options are the engine's own; the
build script adds only how the grid draws each one and the Move-only
controls. `smoke_ui.mjs` still holds the result to what the device reads.

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

THE EDITOR'S FEED IS THE LAST ENTRY, `web_ui` (ni_schwung::WEB_UI_KEY). Its
`extra_keys` are what the module's page in Schwung's web manager is drawn from,
and the manager pushes them live because none of them is a declared param:
`ui` (the pattern and the playhead), `params` (the sound at full precision,
and the Length detents) and `state` (the whole patch, every slot -- the page
renders the gate and envelope curves from it with this engine, built for the
browser). `ui` was once declared as a read-only param. Nothing on the device
read it that way -- ui_chain.js asks for it by name and the gate page lists it
as an extra -- and, declared, it never reached the page.
*/

pub const CHAIN_PARAMS: &str = include_str!(concat!(env!("OUT_DIR"), "/chain_params.json"));

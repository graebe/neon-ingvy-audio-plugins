# NI Side-Chain, on the Move

The Schwung build of the same Rust engine the Live plugin uses. AGENTS.md asks
for every DSP on both hosts, and `sc_render_ab` is what proves these two agree
rather than merely resemble each other.

## What differs from the plugin, and why

**No sidechain source.** The chain host hands over one interleaved buffer and the
API has no aux input anywhere, so `Source` offers Cycle and MIDI here. The option
is absent from `chain_params` rather than present and inert: a control that could
never fire reads as a broken module, not as a platform without the bus.
Threshold and Lockout go with it -- they belong to a detector with nothing to
listen to.

**MIDI has no sample offset.** `move_audio_fx_on_midi` takes no offset, so a note
lands at the top of its block -- up to a block of jitter, which the plugin does
not have because iPlug2 gives `IMidiMsg::mOffset`. That is also what makes the
render A/B meaningful: drive the plugin at offset 0 and the two must agree bit
for bit.

**The id is `ni-side-chain`, not `ducker`.** `charlesvestal/schwung-ducker` already
owns `ducker`, and this is a different module that happens to do a similar job.
Replacing that one is a conversation with its author, not a side effect of
shipping this.

## No `ui_chain.js`

The module serves `ui_hierarchy` as EMPTY -- not -1, which makes the component
entry gate hold for `HOLD_UNSERVED_READ_LIMIT` attempts before falling back --
and supplies no custom page, so the host draws its own knob grid from
`chain_params`.

That is a deliberate v1 scope decision rather than an omission. The grid already
gets the four envelope stages as a declared `viz` group, so the Move draws the
duck's shape from the same numbers the plugin's editor does. A hand-written
canvas page would be a second editor to keep in step with the first, and the
`shape` canvas entry is declared so one can be added later without changing the
parameter contract.

## Building

    ./modules/_shared/package.sh side-chain  # from anywhere, via Docker
    cmake --build build --target schwung-side-chain

    ./modules/_shared/test.sh side-chain     # build, then test on aarch64 in Docker
    ./modules/_shared/install.sh side-chain  # scp to move.local

The build, packaging and install scripts are shared by every module (see
`modules/_shared/`); what is particular to this one is in `module.env`.

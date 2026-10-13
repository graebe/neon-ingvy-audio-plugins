# NI Side-Chain on the Move: the web editor

When NI Side-Chain runs on an Ableton Move, Schwung's web manager can show it
with the same window the plugin has in Live: the duck you drag, the knobs under
it, the trigger's state above it. This folder is that page, `web_ui.html`, a
[Solid](https://solidjs.com) app that the module ships beside its DSP.

It is a port of the native editor (`plugins/side-chain/editor`), not a new
design. Every colour, space and size comes from the Ultraviolet kit
(`web/kit`), and the controls, labels and words are the native editor's.

## Open it on your Move

1. Build the page, then package and install the module:

   ```sh
   scripts/build-web.sh side-chain           # -> build/web/side-chain/web_ui.html
   ./modules/_shared/package.sh side-chain   # puts the page in the module's tarball
   ./modules/_shared/install.sh side-chain   # copies it to move.local
   ```

   `package.sh` takes the page from `build/web/side-chain`. If you have not
   built it, the module is packaged without one, and it tells you so.

2. Load **NI Side-Chain** as an audio effect on a track.
3. On a computer or phone on the same network, open the manager's Remote UI at
   `http://move.local:7700/remote-ui` and pick that track. The editor appears in
   the effect's own section. **Pop out ↗** opens it in a window of its own.
   **Interface: Default** switches back to Schwung's generated knobs, which
   drive the same parameters.

Everything you change is a write to the module, and everything the Move
changes (a knob on the device, a preset, another browser) shows up here. Both
directions go through the same parameters, so there is nothing to sync.

## A tour of the window

**The well** spans exactly one cycle of the duck. The white line is the shape
the engine is running. The faint vertical rule is where the engine is in the
cycle right now, and the ruler below gives the cycle in milliseconds. The ruler
also marks the moment the duck reaches its floor.

**The four handles** on the line are the shape's parameters:

| Handle | Sideways | Up and down |
|---|---|---|
| first, on the top edge | Delay: when the duck starts | |
| second, at the bottom | Attack: how fast it gets there | Depth: how far it goes |
| third | Hold: how long it stays down | |
| last, back on the top edge | Release: how long it takes to come back | |

Hold **Shift** while you drag for fine control. **Double-click** a handle to
reset what it moves. With a handle focused, the arrow keys move it too. A shape
too long to finish inside one cycle is marked with an amber rule at the right
edge.

For example, to make a gentler duck that recovers late: drag the last handle
right until the readout under **Release** says about 300 ms, then drag the
second handle up until **Depth** reads about 60 %. Up is shallower, as the
picture shows.

**The knobs** are the same parameters as numbers. Click a readout to type a
value. For the four stages the unit you type wins: `40 ms` and `8 %` both
work. A bare number is read in whatever unit the readout shows.

**The two rows** below are split by subject. The first is the trigger: Source,
then Rate on Cycle or the Gate switch on MIDI. The second is the shape: Curve,
and **% of cycle**, which switches the stage readouts between milliseconds and
percentages of the cycle. The stages are always stored as percentages, so the
switch changes only how they read.

**The state line** at the top says what is firing the duck and which stage it
is in, for example `CYCLE 1/4 · RELEASE`. At most one amber warning sits at
its right:

- `TRANSPORT STOPPED`: on Cycle, the duck follows the Move's transport, so
  press play.
- `NO MIDI`: on MIDI, no note has arrived for most of a second. Check the note
  and channel.

## How it differs from the plugin

The Move cannot have some of what Live has, so the page leaves it out rather
than showing a control that does nothing:

- **No Sidechain source, Threshold or Lockout.** The Move hands an effect one
  stereo buffer and no key input, so the module declares Cycle and MIDI only.
- **No audio in the well.** The manager carries parameter values to the page,
  not audio. The plugin draws the input and output behind the shape, and the
  kick it ducks to -- the sidechain key or a Listen-In bus, to the sample;
  here the shape is the whole picture. The plugin's `NO INPUT` warning, which
  was read from that audio, goes with it.

## How the page talks to the Move

Schwung's manager loads `web_ui.html` in an iframe and gives it a bridge,
`window.schwungRemote`, from `/static/schwung-remote-api.js`. The page loads
that script before itself. If the script is missing, for example because the
page was opened from disk, the page says where it works instead of showing a
dead editor.

The editor speaks the kit's host vocabulary: a parameter by index, with a
value normalised to 0..1. `src/lib/msg.js` maps each index to the module's
chain key, and the kit's Schwung transport (`web/kit/src/lib/schwung.js`)
does the rest. It reads each key's range and options from the module's
declaration, `chain_params`, so the page has no range of its own to get wrong.

The browser cannot ask the Move for a value. The manager pushes values instead:

| What | When it arrives |
|---|---|
| each declared parameter | once when the page subscribes, then whenever anyone writes it |
| the feeds `ui`, `params`, `stage_ms` | at most 150 ms after any write, and every 500 ms when they moved |

The feeds are the `extra_keys` of the module's last declaration, `web_ui`
(`engines/side-chain/crates/sc-move/src/params.rs`), and their formats are
sc-core's `get_param` (`engines/side-chain/crates/sc-core/src/params.rs`).
`ui` carries the playhead. Because it arrives twice a second at most, the page
carries the position forward between pushes at the cycle's own rate.

## Developing it

You need the repository's npm install and the engine built for the browser:

```sh
npm ci                                     # at the repository root, once
scripts/build-web.sh side-chain            # the engine as wasm, then the page
npx vite --config web/side-chain/vite.config.js
```

Open `http://localhost:47343/harness.html`. The harness is the same editor
against a fake Move: the kit's stand-in for Schwung's manager
(`web/kit/src/lib/fake-schwung.js`), running the real engine compiled to wasm
(`src/harness/engine.js`) at the Move's 44.1 kHz in blocks of 128. It follows
the manager's timing, so what works here works on the device. Add `?stopped`
to start with the transport stopped.

From the browser console, `window.__move` plays the device's side:

```js
__move.device('depth', '0.25')       // turn Depth on the Move
__move.transport({ running: false }) // press stop
__move.note(36, 100); __move.note(36, 0)   // a C1 on channel 1, on and off
__move.get('attack')                 // what the engine holds
__move.writes                        // every write the page made
```

`web_ui.html` itself, opened from the dev server, shows the no-bridge message:
it needs the manager.

### Tests

```sh
node --conditions=browser --test web/side-chain/test/*.test.mjs
npx playwright test --config web/side-chain/playwright.config.mjs
```

The node tests hold the pure parts to their authorities:

- `bindings`: the binding table and every control against the module's
  declaration.
- `declaration`: the ranges and options read from it.
- `feeds`, `engine`: the feed formats against the real engine as wasm. They
  are skipped, and say so, until the wasm is built.
- `status`: the warnings, readouts and typing rules, ported from the native
  editor's `Status.cpp`.
- `shape`, `envelope`: the drawn duck, against tables the engine generated.

The Playwright suite runs in Google Chrome at 760 px and at a phone's 400 px.
`e2e/editor.spec.mjs` drives the harness. `e2e/built.spec.mjs` loads the built
page from `vite preview` on port 47344 with a stand-in bridge script, so build
the page first. After a deliberate visual change, re-baseline the screenshots
with `--update-snapshots`, then run the suite again.

### Where things are

| Path | What it is |
|---|---|
| `web_ui.html`, `src/index.jsx` | the page that ships, and its entry |
| `harness.html`, `src/harness/` | the dev and test page, its fake Move and wasm engine |
| `src/boot.jsx` | starts the editor against a bridge, or says there is none |
| `src/App.jsx`, `src/app.css` | the window: what is drawn, and where it sits |
| `src/lib/layout.js` | every control, as data |
| `src/lib/msg.js`, `declaration.js`, `feeds.js` | the binding table, the declaration, the feeds |
| `src/lib/status.js`, `text.js` | what the window says, and its words |
| `src/lib/Shaper.jsx`, `shape.js`, `Knobs.jsx` | the well, the duck it draws, the stage and choice knobs |

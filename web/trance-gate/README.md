# NI Trance Gate on the Move: the web editor

The Trance Gate's window from Ableton Live — the ring, the envelope, the
panels, the pattern and the pads — running on an Ableton Move, in a browser
tab next to the hardware. Same engine, same patch: what you change here is
what the Move plays, and what you change on the Move shows up here.

It is a [Solid](https://solidjs.com) app, built by [Vite](https://vite.dev)
into one page, `web_ui.html`, which ships in the module's folder. Schwung's
web manager finds it there and shows it in place of the generic knob page.
GPL-3.0-or-later, like everything else in this repository.

## Opening it on a Move

1. Install the NI Trance Gate module (a build of this repository's
   `trance-gate` module that includes the page — see *Building* below).
2. Load **NI Trance Gate** as an audio effect on a track (or on the Master FX)
   on the Move.
3. On a computer or phone on the same network, open Schwung's manager at
   <http://move.local:7700> and go to **Remote UI**. The track's section for
   the effect shows the editor; its pop-out button opens it in a window of its
   own.

The page sizes itself to the section: on a phone the whole window is scaled
to the screen's width, on a desktop it fills the section's width.

## What is on it

Everything works as it does in Live — the [plugin's documentation](../../plugins/trance-gate/docs/live.md)
describes each control — so this is about what is different on the Move.

- **The ring and the pads** edit the pattern: click to toggle a step,
  shift-click for a tie, drag a pad up or down for its amount, sweep the ring
  to paint. Each edit moves the Move's own step cursor to the step you
  touched, exactly as editing on the Move does — that is how the module edits
  a step.
- **The knobs, switches and selects** write the module's parameters, so the
  Move's own knob pages follow them, and a turn of a Move knob moves the knob
  here.
- **Slot** recalls one of the eight slots. Switching a slot on the Move
  brings the new slot's pattern and every knob here too.
- **Randomize** rolls a new pattern into the current slot. On the Move this
  is the **Random** knob turned to *Roll*; the page turns it there and back
  to *Hold*, so the knob is ready for the next roll.
- **Set order** and **Shuffle order** set the order in which **Fade** brings
  the steps in, and a step's number can be clicked and typed, as in Live.
- **The plots** are drawn by the engine itself: the page runs the Trance
  Gate's own DSP, compiled to WebAssembly, on the patch the Move publishes.
- **The playhead** follows the Move's transport. The Move reports it a couple
  of times a second, and the page carries it forward smoothly in between; the
  background ripples on the beat when **Motion** is on.

### What Live has and this page does not

| In Live | On the Move | Why |
|---|---|---|
| The **Signal** plot: your audio before and after the gate | not here | no audio reaches a browser page from the Move |
| **Copy** and **Paste** a slot | not here | the manager's frame has no clipboard |
| **Export** and **Import** slot and bank files | not here | the manager's frame cannot download or open files |

## How it talks to the Move

The page loads the manager's bridge, `/static/schwung-remote-api.js`, which
gives it `window.schwungRemote` for the component it drives (`fx1`,
`master_fx:fx2`, …). Nothing is asked for: the manager pushes.

| what | how it arrives or leaves |
|---|---|
| the fifteen sound values (Slot, Length, Rate, … Fade Dir) | from the engine's `params` readout, re-read at most 150 ms after any write and twice a second; written as the module's own keys (`slot`, `hold` for Width, `fade_dir`, …) |
| the pattern, the cursor and the playhead | the `ui` readout, on the same schedule |
| the whole patch | the `state` readout, which the page renders its curves from |
| a step's mode, amount or arrival | `cursor`, then `step` (*Off*, *On*, *Tie*), `step_amount` or `step_order` |
| Randomize | `randomize` = *Roll*, then *Hold* |

The readouts are the `extra_keys` of the module's last `chain_params` entry,
`web_ui` (`engines/trance-gate/crates/tg-move/src/params.rs`). `src/lib/move.js`
is the whole table, and the kit's transport (`web/kit/src/lib/schwung.js`)
carries it.

**The sound comes from the `params` readout, not from echoes.** A slot recall
on the Move writes one parameter and changes fourteen, so the page takes each
of the fifteen values from the readout — what the engine actually holds —
and never from the manager's echo of a write.

## Developing

Everything runs from the repository root, after one `npm ci` there.

```sh
scripts/build-web.sh trance-gate     # the engine's wasm, then the page -> build/web/trance-gate
npx vite --config web/trance-gate/vite.config.js
                                     # http://localhost:47341/harness.html
```

**`harness.html` is the editor on a Move that is not there:** the kit's fake
manager (`web/kit/src/lib/fake-schwung.js`) with the module's real engine
behind it, the same wasm, and the module's own `chain_params` read from
`params.rs` — so a range cannot differ from the device's. In the browser's
console, `window.__move` plays the Move's side:

```js
__move.device('slot', '3')     // turn the Move's Slot knob to slot 4
__move.device('rate', '1/8')   // or any other key, as the Move writes it
__move.stop(); __move.play()   // the transport
__move.writes                  // every write the page has made
__move.get('pattern')          // what the engine holds now
```

The build writes `build/web/trance-gate/`: `web_ui.html`, `assets/ui.js`,
`assets/style.css` (the font inlined), `assets/tg_web.wasm`,
`assets/ui.js.LICENSE.txt` and `fonts/OFL.txt`. `modules/_shared/package.sh`
puts them in the module's tarball.

### Tests

```sh
# quick: the table, the readouts, the stages, the engine's renders
node --conditions=browser --test web/trance-gate/test/*.test.mjs

# end to end, in Chrome, desktop and a 400 px phone: the harness on :47341
# and the BUILT page on :47342, so build first
scripts/build-web.sh trance-gate
npx playwright test --config web/trance-gate/playwright.config.mjs
```

The node tests that need the engine read `target/wasm32-unknown-unknown/release/tg_web.wasm`
and fail with *run scripts/build-web.sh trance-gate* when it is not there.
The oracle tables in `test/` are the engine's own measurements — the curve
shape, the fade's weights and the envelope — and the JavaScript that mirrors
the DSP is held to them.

Screenshots live in `e2e/__screenshots__`; after a deliberate change to the
picture, run Playwright once with `--update-snapshots` and look at what it
wrote before committing it.

### The rules this page keeps

- **Ultraviolet tokens only.** Every colour is a `var(--token)` from
  `web/kit/src/tokens.css`; no colour is spelled in this directory.
- **The page is as tall as it is drawn,** never sized by the viewport: the
  bridge reports the body's height to the manager, which sizes the frame
  from it (`.page` in `src/app.css`).
- **No dead controls.** What cannot work in the manager's frame is left out,
  not greyed.

## Where things are

| file | what |
|---|---|
| `web_ui.html`, `src/main.jsx` | the page and its entry: the Move's bridge, or a line saying there is none |
| `harness.html`, `src/harness.jsx` | the page on the fake Move, for development and tests |
| `src/boot.jsx` | the transport and the engine's wasm, then the editor |
| `src/App.jsx`, `src/app.css` | the window, and the state its pieces share |
| `src/lib/move.js` | the editor's parameters, messages and readouts in the module's terms |
| `src/lib/renders.js`, `src/lib/capture.js` | the plots' curves, rendered by the engine and decoded |
| `src/lib/stages.js` | Attack, Decay and Release in ms or %, as Live shows them |
| `src/lib/wasm-engine.js` | the engine behind the fake Move |
| `src/lib/*.jsx` | the ring, the pads, the panels, the row and the plots |

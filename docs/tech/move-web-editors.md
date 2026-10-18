---
title: Web editors on the Move
order: 8
slug: move-web-editors
---

Each Move module carries a web page that looks and works like the plugin's
editor: `web_ui.html`, in the module's folder. Schwung's web manager
(`http://move.local:7700` › Remote UI) shows it in place of the generic knob
grid for the slot the module sits in. The page edits the module running on the
Move. It makes no sound and holds no state of its own.
[ADR 0004](../adr/0004-move-web-editors.md) records why it exists and what it
costs.

```
 web/<module>  (Solid)                  Schwung's web manager            the module (Rust)
 ─────────────────────                  ─────────────────────            ─────────────────
 editor ── host.js ── schwung.js  ◄──── param_update ◄── notify ring ◄── set_param (any writer)
                         │        ◄──── param_update ◄── extras push ◄── get_param(feed)
                         └──────────►   set_param ──────► web-set ring ─► set_param
 <p>-web.wasm (the same engine) ── draws what the plugin's editor drew from the engine
```

## How a value reaches the page

The page cannot ask the Move for a value. The manager pushes values, in two
ways, as `<component>:<key>` strings. The component is the slot position the
page was opened for: `fx1`, `fx2`, `master_fx:fx1` and so on.

- **Declared parameters** (the module's `chain_params`). The manager sends each
  one once when the page subscribes, and again whenever anybody writes it: the
  Move's knobs and jog, another browser, this page.
- **Feeds.** These are the `extra_keys` of the module's last declaration,
  `web_ui`. The manager re-reads them at most 150 ms after any write, and twice
  a second regardless, and sends those that changed.

A value the engine changes by itself is written by nobody. The playhead and a
slot recall are two examples. Such a value can only reach the page as a feed.
The manager never sends a feed key that is also a declared parameter. That is
why `ui` is a feed and no longer a declared read-only parameter.

| module | feeds | what the editor draws from them |
| --- | --- | --- |
| Trance Gate | `ui`, `params`, `state` | the pattern and the playhead; the sound at full precision and the Length detents; the patch, for the curves |
| Side-Chain | `ui`, `params`, `stage_ms` | the trigger, the sweep and the stage; the engine's values in their own units; each stage in ms |

`ni-schwung` answers `web_ui` (`WEB_UI_KEY`) with the contract's version. Each
module's tests check that `web_ui` is the last declaration, is read-only, and
names at most four feeds, none of them declared and all of them answered.

A write from the page is `setParam('<component>:<key>', value)`. The value
must be under 256 bytes. Writes arrive in order and none are lost. The
editors keep their parameter-by-index vocabulary (`web/kit/src/lib/host.js`).
`web/kit/src/lib/schwung.js` maps an index to a module key. It converts the
normalised value to the module's wire value and display text from the
`chain_params` entry (`lib/chain.js`), so the page never invents a range of
its own. Editor messages that are not parameters become writes. For example,
the Trance Gate's "set step 5 to On" becomes `cursor` = 4 and then `step` =
`On`.

## The playhead

A feed reaches the page anything from about 20 to 400 ms after it was true,
and each one is late by a different amount: the manager's re-read and
heartbeat, the shim, WiFi, the iframe. A playhead carried from each feed's
arrival is pulled back by that feed's delay, so it wobbles and lurches.

So each engine stamps its `ui` readout with its own clock: `devms`, the time
it had processed when the readout was true. It counts frames as a `u64` and
appends the field, never reordering. The page's clock (`web/kit/src/lib/clock.js`
on `lib/clocksync.js`, the same estimator as Piano Practice's) learns the
offset between the engine's clock and its own from the smallest delay it has
seen; a readout can be late but never early. It then draws:

- where the engine is *now*;
- corrections eased over about 100 ms, including the clock's own: a readout
  that teaches it a smaller delay;
- a moving playhead that never steps back;
- a real jump, such as a restart or a new trigger, at once.

A position that wraps (the Trance Gate's step in its length, the Side-Chain's
sweep in its cycle) passes its `period` to the clock. A readout from after
the wrap is compared with what is drawn modulo that period, so the wrap is no
jump; without it, every wrap snapped, and drew whatever correction was still
being eased as one step.

What remains is a steady lag the size of the smallest delay, which does not
move. Each harness's `?jitter` delays every push by 20–400 ms. Each editor's
`e2e/sync.spec.mjs` holds the drawn playhead to the engine through that, and
fails without `devms`. The Side-Chain's spec times each step from when the
line was drawn where it left from, because the clock re-renders on a timer
and a machine starving that timer holds the line for a few frames.

## The engine in the browser

Some things the plugin's editor draws come from the engine, not from a
model. The Trance Gate's gate and envelope curves are one example: a scratch
engine renders the patch, so the picture is the sound. The web editor uses
the same engine. `engines/<p>/crates/<p>-web` builds the product's C ABI,
unchanged, for `wasm32-unknown-unknown`. It adds `<p>_web_alloc` and `_free`,
because a browser caller cannot bring its own memory. `web/kit/src/lib/capi.js`
handles calling it. The Trance Gate's page fetches `tg_web.wasm` and
renders the curves from each new `state`.

## Building and packaging

```
scripts/build-web.sh [module]      the engine's wasm, then vite build -> build/web/<module>/
modules/_shared/package.sh <m>     ships build/web/<m>/ beside the module's .so
```

`web/kit/vite.editor.mjs` is every editor's Vite config.

- **One file each.** The page is one `ui.js` and one `style.css`, with the
  fonts inlined. The manager rebuilds the iframe often and serves files with
  `no-store`, so a cold start is the common case.
- **Relative URLs.** The page lives wherever the module does.
- **The bridge loads first.** `web_ui.html` loads
  `/static/schwung-remote-api.js` from the manager before the editor.

A module packaged without a build ships without its page. The packaging step
says so. The device's own controls work without the page.

## Developing and testing

`npx vite --config web/<module>/vite.config.js` serves the editor on its own
port, 47341 for the Trance Gate and 47343 for the Side-Chain. `harness.html`
runs the editor against a fake Move (`web/kit/src/lib/fake-schwung.js`):

- **The manager's rules.** The fake applies the manager's rules from
  `remote_ui.go`.
- **The real engine.** Behind the fake is the module's real engine, as wasm.
- **The real declaration.** `chain_params` is the one the module's build
  script writes from its engine's table, read where cargo says it wrote it
  (`virtual:chain-params`).

`window.__move` stands in for the Move's own controls.

| test | tier | what |
| --- | --- | --- |
| `web_kit` | quick | the kit: host, transport, `chain_params` conversion, fake Move, tokens, components |
| `web_tg`, `web_sc` | quick | an editor's node tests, against its engine's wasm |
| `web_tg_e2e`, `web_sc_e2e` | full | the editor built and driven in Chrome against the fake Move, and the built `web_ui.html` against a fake bridge |
| `tg_core_rs`, `sc_core_rs` | quick | include `tg-web`, `sc-web` and each module's `web_ui` contract |

`scripts/test-web.sh` runs them, and skips them when `node_modules` is
missing.

## What the Move does not give a page

- **No audio.** There is no scope.
- **No downloads or clipboard in the iframe.** So there is no export, import,
  copy or paste. The manager's pop-out window is unsandboxed and could carry
  them later.
- **No Master FX feeds.** The manager pushes `extra_keys` for slot components
  only. On a Master FX position the page gets declared values, but its
  playhead and readouts stand still.

Two manager bugs are on record in ADR 0004, to be reported to Schwung.

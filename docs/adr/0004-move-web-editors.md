# 0004. The Move modules get web editors, from the Solid kit

- Status: accepted, 2026-10-10
- Decided by: Torben Gräber ("a UI like my plugin … just like in Ableton Live
  but remote on the Schwung", 2026-10-09)
- Amends: [0002](0002-juce-native-editors.md), which retired the Solid kit
  and kept Solid and Vite for browser apps

## Context

The Move modules (`modules/trance-gate`, `modules/side-chain`) are driven from
the Move's knobs, jog and pads. Schwung's web manager (`move.local:7700`,
Remote UI) shows a module's controls in a browser as a generic grid. A module
can replace that grid with a page of its own, `web_ui.html`, shown in a
sandboxed iframe for the module's slot. That page is a browser app, and
0002 keeps Solid and Vite for browser apps.

The plugins' editors are native JUCE, and JUCE cannot run in a browser:
JUCE 9's WebAssembly support covers `juce_core` only, and using a fork would
be a workaround. The Solid editors that the JUCE ones were drawn from still
exist in history (8ed8ac9), together with their kit.

## Decision

- **Each Move module ships a web editor** as its `web_ui.html`, built from
  `web/<module>`. It looks and behaves like the plugin's editor, within what
  the Move and the iframe allow.
- **The Solid kit returns as `web/kit`**, restored from 8ed8ac9. It does not
  return to the plugins: they stay native (0002).
- **One vocabulary, a pluggable host.** The kit's `iplug.js` became `host.js`.
  The editors keep the vocabulary they had: a parameter by index, normalised,
  and tagged messages each way. A transport carries it, and the one transport
  is Schwung's (`lib/schwung.js`).
- **The engine decides, in the browser too.** Wherever the plugin's editor
  drew from the engine, the web editor draws from the same engine, compiled to
  wasm (`engines/<p>/crates/<p>-web`). The Trance Gate's plot curves are one
  example. Nothing is modelled twice.
- **The tokens are generated** from `design/scheme/project/tokens.json`, beside
  the native header, by `scripts/gen-tokens.mjs`.
- **No change to Schwung.** The module feeds its page through Schwung's own
  contract: a trailing `web_ui` declaration whose `extra_keys` name the
  readouts the editor draws from.

## Consequences

- **Two editors per product.** They share the engine, the parameters'
  declarations and the tokens. Their drawing code does not overlap. A change
  to a plugin's editor is a change to make in its web editor too.
- **A page can only show what the Move publishes.** There is no audio for a
  page, so there is no scope. The iframe blocks downloads and the clipboard,
  so there is no export, import, copy or paste yet. The pop-out window could
  carry them later.
- **Values arrive at the manager's pace.** The page gets a declared value
  when someone writes it, and the feeds up to twice a second. A playhead is
  carried forward between feeds, as the device's own screen does.
- **Two Schwung bugs are worked with, not around, and are to be reported
  upstream.**
  - The manager drops the last batch of a component's initial values when
    its last param cannot be read.
  - It never pushes a declared read-only param that the DSP changes by
    itself.

  The `web_ui` feed would exist without either bug. When the first is fixed,
  nothing here changes.

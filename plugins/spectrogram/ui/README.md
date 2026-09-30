# The editor

A [Solid](https://solidjs.com) app built by Vite into `../resources/web`, loaded
by iPlug2's WebView editor over a custom URL scheme. MIT, like everything else
here.

```
npm ci               # at the repository root: one install for every workspace
npm run build        # -> ../resources/web (build output, untracked), which CMake globs into the bundle
npm run dev          # live-reload; point mEditorInitFunc at localhost:5173
test/harness/build.sh   # the built bundle against a fake plugin, in a browser
```

The harness page needs a **server**, not a `file://` URL: the editor is a module
script and a browser refuses to load one off the filesystem. Any static server
rooted at `test/harness` will do (`python3 -m http.server`), and `?freeze` holds
the picture after two seconds so a screenshot catches a full view.

## What talks to what

The UI decides **nothing** about the analysis. The band count, the log frequency
mapping, the window length and the dB floor all live in the Rust analyzer
(`engines/spectro`), and the frequency scale is drawn from the centres it sends.

**Pause is never sent.** It holds the *view*: columns keep arriving and keep
being written into the history canvas behind the frozen picture, and only the
repaint stops — so unpausing shows a view that is already current, with the
paused seconds present in it rather than cut out of it. The plugin is not told
because there is nothing for it to do.

The range is the one thing the editor does tell the plugin, and the plugin
answers with the new scale rather than letting the editor assume its request was
honoured: `f_max` is clamped to Nyquist, so in a 32 kHz session "High" really is
2 k to 16 k.

| tag | direction | carries |
|---|---|---|
| `64` | → UI | `"<ch>:<cols>:<bands>:<hex>"` — finished columns, one byte per band, oldest first. One message **per source**: the payload budget is a product, and three channels at the full catch-up budget overflows the cap |
| `65` | → UI | the band centre frequencies in Hz, comma separated |
| `96` | → plugin | `"<f_min>:<f_max>"` — the range dropdown's zoom |
| `120` | → plugin | `ready`: "mounted, send me the axis" — the shell's tag, the same in every plugin |

`ready` is not optional. The plugin also pushes the axis from `OnUIOpen`, but that
fires before this deferred module has evaluated, so it lands on an undefined
global and is dropped. `test/harness/mock.js` reproduces that ordering on
purpose — if you make it deferred, you have disabled the test.

## The files

| | |
|---|---|
| `src/uv.css` | the Ultraviolet tokens, transcribed. **The only place a colour may be spelled** — `ui/test/tokens.test.mjs` fails the build otherwise |
| `@ultraviolet/ui` (ramp) | level → colour, reading the five `--spec-*` stops back out of the stylesheet, because a canvas cannot use a CSS variable |
| `@ultraviolet/ui` (Spectrogram) | the picture: a ring of columns on an offscreen canvas, two `drawImage` calls to show it, and the crosshair over it |
| `src/lib/columns.js` | the wire, decoded. Plain JS so node's test runner can import it |
| `src/App.jsx` | the layout, the scale, the hint bar |

---
title: Testing
order: 6
slug: testing
---

Two tiers, one entry point. The **quick** tier is the loop you run while
working; the **full** tier is the verification before you call something done.

```sh
scripts/test.sh quick                  # npm run test:quick
scripts/test.sh full [--bundles DIR]   # npm run test:full
```

Both configure `build/` with `-DIPLUG_DEPLOY_PLUGINS=OFF` if it does not exist
yet, so nothing is ever installed into `~/Library/Audio/Plug-Ins`.

## Quick — the developer loop

`cmake --build build --target ni_tests` builds the test programs and the engine
archives they link, and no plugin bundle; then `ctest -L quick` runs:

- every Rust crate's unit tests (`cargo test`, crate by crate),
- the C tests against each engine's hand-written ABI, and the oracles that pin
  the editors' maths to the engine's measured output,
- the doctest wire, state and parameter tests (`tests/cpp`),
- all of the kit's and the editors' JavaScript (`ui_unit`, the same files
  `npm test` runs),
- the lint-like checks: `versions`, `release`, `licenses` (the tree, not the
  bundles), `ui_tokens`, `editor_tags`, `editor_timing` (no editor code may hang off
  `requestAnimationFrame` or page visibility), `ground_shells`.

No bundle, no host, no browser, no timing. Warm, it takes a few seconds.

## Full — final verification

Everything quick runs, and:

| label | what |
|---|---|
| `render` | the render A/B goldens: four seconds through each plugin's audio path, hashed |
| `host` | the AUs from `build/out`, loaded by path: `tg_au`, `sc_au` render through a host that supplies a transport; `au_stress_*` runs auval's stress pattern on each; `au_ground_*` opens each one's real editor and plays silent audio at 120 BPM, and the page must receive a ring a beat, every fourth strong, and none once stopped; `editor_host_*` opens every plugin's real editor as a VST3, an AU and a CLAP host would, feeds them audio under a running transport, closes and reopens them, and asks the page what reached it; `iplug2_fixtures` reopens the saved states in `tests/fixtures/iplug2` in the VST3s from `build/out`, and every parameter must read what was captured |
| `ipc` | the bus written in one process and read in another — and, on an arm64 Mac with Rosetta, between the x86_64 and arm64 slices both ways round |
| `bundles` | every built bundle carries its notices |
| `site` | every root-relative link on the built site resolves |
| `e2e` | the four editors in Chrome against their mock hosts (below) |
| `coverage` | the coverage floor, in the instrumented build |

`scripts/test.sh full` builds everything and the site, runs `ctest -L full`,
then `scripts/coverage.sh` with the floor enforced, then `auval`, `pluginval`
and `clap-validator` over the bundles in `build/out` (or `--bundles DIR`).

**clap-validator is held to a manifest, not to zero.** Some of its failures are
iPlug2's, fixed by the patches in `docs/iplug2-patches` that are proposed but
not applied, and one is clap-validator's own bug.
`tests/validators.known.json` lists each by bundle and test id with the patch
that fixes it, and `scripts/validator-verdict.mjs` fails the stage on any
failure it does not list *and* on any listed failure that now passes — so the
list can only shrink. Warnings are printed and never fail.

**What it reads outside the checkout.** Only in the validator stage: `auval`
and pluginval's AU pass find their component through the system's registry,
which lists *installed* components, so they validate the installed AU — on a
machine that has not installed this build, an older one. They stay there as
validators of what is installed. The VST3 and CLAP bundles are validated from
the bundle directory.

**The AU tests in ctest never read what is installed.** `tests/au_bundle.h`
loads a bundle from `build/out` by path and hands its factory to
`AudioComponentRegister`, Apple's API for a component implemented inside the
calling process: the registration is visible to that process alone, under the
test-only manufacturer `NiTs`, so an installed copy under the same triple can
neither be tested by mistake nor shadow the build. Type, subtype and factory
come from the bundle's own `Info.plist`. A missing bundle fails; it never skips.

**`editor_host_<Plugin>_<format>`** (`tests/editor_host.mm`) is the
plugin-to-editor path with nothing mocked but the DAW: the bundle from
`build/out` in-process, iPlug2's WKWebView with the shipped page, a sine and a
kick on a render thread under a transport playing at 120 BPM, and the editor
opened and closed through the format's own calls (VST3 `attached`/`removed`,
the AU's view factory and `removeFromSuperview`, CLAP
`set_parent`/`hide`/`destroy`). The page is then asked what it received: a
`ready` answered and a ring on each beat of the transport, for every editor;
for the Spectrogram, column batches carrying the sine and the columns that
arrived on the visible canvas; for the Trance Gate and the Side-Chain, a
playhead that moves with the transport; for the Listen-In, the bus's state and
name, and a fresh bus's name field empty rather than `(null)`. Each check
waits for its condition rather than a fixed time, up to a minute, so a busy
machine is slower to pass, never failed. It needs a logged-in session; the
window may sit behind others, and
the editors have to survive WebKit calling the page hidden -- no animation
frames, throttled timers (`editor_timing` is the rule that keeps them
independent of both).

**`iplug2_fixtures`** (`tests/vst3_capture.mm --check`) guards the sets
saved with the iPlug2 VST3 builds while those builds exist. The fixtures in
`tests/fixtures/iplug2` are what v2026.10.06.5 saved, captured through the
calls Live makes ([README](../../tests/fixtures/iplug2/README.md),
[FORMAT](../../tests/fixtures/iplug2/FORMAT.md)). Each is loaded the way a host
reopens a set, and every parameter must read what was captured. Bytes are not
compared, because a later engine may write the same patch differently.

**`au_stress_<Plugin>`** is `auval -stress`'s state path, for every plugin:
a render thread, two threads getting and setting `ClassInfo` (the plugin's
`SerializeState`/`UnserializeState`, on threads that are not the main thread),
and the main run loop, so iPlug2's idle timer services what they recorded. A
watchdog turns a hang into a failure naming the stuck thread; a crash fails
as a crash. Three seconds a plugin by default; `-DNI_AU_STRESS_SECONDS=60`
makes it a soak, and `build/tests/au_stress <bundle> [seconds] [threads]` runs
one by hand — against any bundle, an installed one included, read-only.

## Labels, not lists

Every test is placed in a tier by `ni_test_tiers()` at the end of the
`CMakeLists.txt` that registers it (`cmake/NiTest.cmake`). A quick test carries
`quick;full`, so `ctest -L full` is literally everything; a full-only test
carries `full` and a label saying why — `ctest -L e2e` runs one kind alone. A
test registered in no tier stops the configure: a new test is placed on
purpose, never by default.

## End to end

`tests/e2e` drives each editor's review harness — the bundle the plugin ships,
beside a mock host that records every message the editor sends — in the Google
Chrome already installed; Playwright downloads no browser. The harnesses are
built first and served on their own port, `47219` (`NI_E2E_PORT`), so the suite
cannot collide with a dev server.

```sh
npm run test:e2e      # or: ctest --test-dir build -L e2e
```

Each editor is tested through what a user does: the ready handshake with a
clean console, a reset to the plugin's defaults, a knob drag as one gesture
that a host echo cannot move, typed values, the keyboard, the resize, and
each editor's own flows. A screenshot per editor is held to a baseline in
`tests/e2e/__screenshots__`, taken with Motion off under a paused clock, so the
picture does not depend on the machine's speed. After a deliberate visual
change, look at the diff in `build/e2e/report`, then:

```sh
npx playwright test --config tests/e2e/playwright.config.mjs --update-snapshots
```

## Coverage

`scripts/coverage.sh` measures the C, the C++, the Rust and the JavaScript —
the kit's libraries under node, and every component under Chrome through the
e2e suite and the bundle's source map — into one report in `build/coverage/`.
`tests/coverage.floors.json` holds the 80 % floor and it is enforcing: a unit
below it, or a first-party file no test loads, fails. The only exemptions are
what cannot be built into anything a test runs, or driven by one — the Schwung
module's `ui_chain.js`; the five plugin-class files that compile only inside a
plugin-format target; and the two AppKit glue files, `FileDialog.mm` and
`Clipboard.mm`, which also compile only there, and which need a person to answer
a save panel or would overwrite the clipboard of whoever runs the tests — and
each says why.

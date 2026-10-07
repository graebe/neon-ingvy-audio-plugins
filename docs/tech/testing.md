---
title: Testing
order: 6
slug: testing
---

Two tiers, one entry point. The **quick** tier is the loop you run while
working; the **full** tier is the verification before you call something done.

```sh
scripts/test.sh quick [--build DIR]                  # npm run test:quick
scripts/test.sh full [--build DIR] [--bundles DIR]   # npm run test:full
```

Both configure `build/` (or `--build DIR`, one per person or agent building at
the same time) with `-DIPLUG_DEPLOY_PLUGINS=OFF` if it does not exist yet, so
nothing is ever installed into `~/Library/Audio/Plug-Ins`.

## Quick — the developer loop

`cmake --build build --target ni_tests` builds the test programs and the engine
archives they link, and no plugin bundle; then `ctest -L quick` runs:

- every Rust crate's unit tests (`cargo test`, crate by crate) — among them
  the no-allocation checks, the two-thread stress runs and the property tests,
  whose seed is fixed so the tier's verdict never changes without a commit —
  and `cargo_deny`, the licence gate over their dependency graph (below),
- the C tests against each engine's hand-written ABI, and the oracles that pin
  the editors' maths to the engine's measured output,
- the doctest wire, state and parameter tests (`tests/cpp`), the JUCE shell's
  state codec over every product's iPlug2 fixture (`nist_fixtures`), NI Trance
  Gate's processor, model and editor in one program (`tg_processor`), and its
  audio callback under an allocation guard (`tg_rt`, macOS); NI Spectrogram's
  the same, with its session on its own (`tests/spectrogram`: `sg_processor`,
  `sg_rt`, `spectro_state`, `spectro_wire`); NI Listen-In's the same, on a
  bus namespace of its own (`tests/listen-in`: `li_processor`, `li_rt`); NI
  Side-Chain's the same (`tests/side-chain`: `sc_processor`, `sc_rt`), its
  MIDI and panic and its key bus included,
- the native kit's and editors' unit tests (`tests/ui`; their snapshot
  goldens are in the full tier),
- all of the kit's and the editors' JavaScript (`ui_unit`, the same files
  `npm test` runs),
- the lint-like checks: `versions`, `release`, `licenses` (the tree, not the
  bundles), `spdx` (every source file opens with its licence and copyright),
  `ui_tokens`, `editor_tags`, `editor_timing` (no editor code may hang off
  `requestAnimationFrame` or page visibility), `ground_shells`, `design_paths`
  (every path into `design/` that a tracked file names exists, however it is
  spelled), `validator_verdict` (the verdict logic over sample clap-validator
  output, and the manifest itself; no validator runs).

No bundle, no host, no browser, no timing. Warm, it takes a few seconds.

## Full — final verification

Everything quick runs, and:

| label | what |
|---|---|
| `render` | the render A/B goldens: four seconds through each plugin's audio path, hashed |
| `host` | NI Trance Gate's VST3 from `build/out`, hosted by JUCE (`tg_host`): its iPlug2 class, its parameters through the controller, every fixture reopened and saved back byte for byte, the golden render through its audio path, and the window under a running transport with a set loaded on another thread; NI Spectrogram's (`sg_host`) the same, its audio through bit for bit where the Trance Gate's renders; NI Listen-In's (`li_host`) the same, its audio through bit for bit and read back off the bus, a reopened set's bus claimed under its name, and nothing published while the host bypasses it; NI Side-Chain's (`sc_host`) the same, the engine's golden render through it bit for bit, and a note, CC 120 and CC 123 through the host's event list and MIDI-CC mapping, and a key on its sidechain bus; `juce_host_*` runs, saves and opens every bundle on the JUCE shell. The AUs of the products still on iPlug2 from `build/out`, loaded by path: `au_stress_*` runs auval's stress pattern on each; `au_ground_*` opens each one's real editor and plays silent audio at 120 BPM, and the page must receive a ring a beat, every fourth strong, and none once stopped; `editor_host_*` opens every plugin's real editor as a VST3, an AU and a CLAP host would, feeds them audio under a running transport, closes and reopens them, and asks the page what reached it; `iplug2_fixtures` reopens the saved states in `tests/fixtures/iplug2` in the VST3s from `build/out`, and every parameter must read what was captured |
| `ipc` | the bus written in one process and read in another — and, on an arm64 Mac with Rosetta, between the x86_64 and arm64 slices both ways round |
| `bundles` | every built bundle carries its notices; every JUCE bundle's signature verifies as Live's scanner checks it (`codesign --verify --deep --strict`); the bundles' version spellings (`versions_bundles`) |
| `site` | every root-relative link on the built site resolves |
| `e2e` | the four editors in Chrome against their mock hosts (below) |
| `coverage` | the coverage floor, in the instrumented build |

`scripts/test.sh full` builds everything and the site, runs `ctest -L full`,
then `scripts/coverage.sh` with the floor enforced, then `auval`, `pluginval`
and `clap-validator` over the bundles in `build/out` (or `--bundles DIR`), and
`pluginval` with its editor tests and Steinberg's VST3 `validator` over every
bundle on the JUCE shell. `--build DIR` uses another build directory.

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
for the Spectrogram, column batches carrying the sine, the fortieth within ten
seconds of the first, and the columns that arrived on the visible canvas; for
the Trance Gate and the Side-Chain, a playhead that moves with the transport,
its third position within five seconds of its first; for the Listen-In, the
bus's state and name, and a fresh bus's name field empty rather than `(null)`.

With the editor open, the plugin's own state is then saved and loaded back
through the format's calls (VST3 `getState`/`setState`/`setComponentState`, the
AU's `ClassInfo`, CLAP `state`) on a thread that is not the main one, as an AU
host may and a careless VST3 or CLAP host can; iPlug2 reports the load from
that thread. WKWebView's `evaluateJavaScript:` is swizzled for the whole run,
so a call from any thread but the main one is counted and dropped: there must
be none, and every value and display string must reach the page afterwards,
from the shell's next idle tick.

Each check waits for its condition rather than a fixed time, up to a minute,
so a busy machine is slower to pass; only the two rates above are held to a
clock, each at least ten times what an idle machine needs. It needs a
logged-in session; the window may sit behind others, and the editors have to
survive WebKit calling the page hidden — no animation frames, throttled timers
(`editor_timing` is the rule that keeps them independent of both).

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

## Licences

The project is GPL-3.0-or-later
([docs/adr/0001-gpl-3.0-or-later.md](../adr/0001-gpl-3.0-or-later.md)), and
three quick-tier tests hold it to that:

| test | what it holds |
|---|---|
| `spdx` | every source file this repository owns opens with `SPDX-License-Identifier: GPL-3.0-or-later` and `Copyright (C) 2026 Torben Gräber`, in its own comment syntax. `tests/spdx.test.mjs` lists what is excluded and why: external code, the design mirrors, the web editors this refactor deletes, Schwung's vendored headers |
| `cargo_deny` | `cargo deny check licenses bans sources`: every crate in the graph is under a licence on `deny.toml`'s allowlist and comes from crates.io, and a crate in two versions is shown |
| `licenses` | `LICENSE` is the unmodified GPLv3, `THIRD_PARTY_LICENSES.md` lists exactly what ships, and the Rust crates' section of it lists exactly the crates and versions `cargo metadata` says ship |

The Rust crates' section of `THIRD_PARTY_LICENSES.md` is generated, never
written by hand:

```sh
scripts/gen-rust-notices.sh      # after anything changes Cargo.lock
```

It runs cargo-about with `about.toml` and the template
`scripts/rust-notices.hbs`, and replaces the text between the section's two
markers. `licenses` fails until it has been run.

**The two tools are pinned**: cargo-deny **0.20.2** and cargo-about **0.9.2**.
`deny.toml`'s schema has changed under cargo-deny before, and two versions of
cargo-about may render the same graph differently. `scripts/licence-tools.sh`
is the one place the versions are spelled:

```sh
scripts/licence-tools.sh install     # cargo install --locked, both, at their pins
scripts/licence-tools.sh verify      # names what is missing or at another version
```

They are developer tools, installed into cargo's own `bin` directory and
linked into nothing. Without cargo-deny, `cargo_deny` fails rather than
skipping, and configure warns first. The generator refuses any cargo-about but
the pinned one.

**Advisories are not part of the quick tier.** They come from a database
fetched at check time, so the verdict would change without a commit.
`cargo deny check advisories` is the separate question, asked on demand.

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
module's `ui_chain.js`, and the plugin-class files of the products still on
iPlug2, which compile only inside a plugin-format target — and each says why.
A product on the JUCE shell has no such file: its processor, model and editor
compile into a test program (NI Trance Gate's `tg_processor`).

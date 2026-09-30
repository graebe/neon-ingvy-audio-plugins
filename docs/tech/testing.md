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
  bundles), `ui_tokens`, `editor_tags`, `ground_shells`.

No bundle, no host, no browser, no timing. Warm, it takes a few seconds.

## Full — final verification

Everything quick runs, and:

| label | what |
|---|---|
| `render` | the render A/B goldens: four seconds through each plugin's audio path, hashed |
| `host` | `tg_au`, `sc_au`: the installed AU rendered by a host that supplies a transport |
| `ipc` | the bus written in one process and read in another — and, on an arm64 Mac with Rosetta, between the x86_64 and arm64 slices both ways round |
| `bundles` | every built bundle carries its notices |
| `site` | every root-relative link on the built site resolves |
| `e2e` | the four editors in Chrome against their mock hosts (below) |
| `coverage` | the coverage floor, in the instrumented build |

`scripts/test.sh full` builds everything and the site, runs `ctest -L full`,
then `scripts/coverage.sh` with the floor enforced, then `auval`, `pluginval`
and `clap-validator` over the bundles in `build/out` (or `--bundles DIR`).

**What it reads outside the checkout.** Only Audio Units: `tg_au`, `sc_au`,
`auval` and pluginval's AU pass find their component through the system's
registry, which lists *installed* components, so they test the installed AU
(`tg_au` refuses one whose version is not this tree's). The VST3 and CLAP
bundles are validated from the bundle directory.

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
what cannot be built into anything a test runs — the Schwung module's
`ui_chain.js`, and the five plugin-class files that compile only inside a
plugin-format target — and each says why.

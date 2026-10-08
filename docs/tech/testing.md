---
title: Testing
order: 6
slug: testing
---

Two tiers, one entry point. The **quick** tier is the loop you run while
working; the **full** tier is the verification before you call something done.

```sh
scripts/test.sh quick [--build DIR]                                  # npm run test:quick
scripts/test.sh full [--build DIR] [--bundles DIR] [--no-coverage]   # npm run test:full
```

Each tier configures its build directory from a preset if it does not exist
yet: quick from `dev` into `build-dev/`, full from `release` into `build/`
(below). `--build DIR` names another one — one per person or agent building
at the same time — and a directory that exists is used as it was configured.
No build installs anything: a bundle is copied into `<build>/out`, where every
test and validator loads it by path, and into the system's VST3 folder only
with `-DNI_DEPLOY_PLUGINS=ON`, which no preset sets. Every stage of either
tier is timed into the timing log (below).

## Presets: which build when

`CMakePresets.json` names the three builds this repository makes. All three
use Ninja.

| preset | directory | what it is | use it |
|---|---|---|---|
| `dev` | `build-dev/` | this machine's architecture only, `RelWithDebInfo`, no link-time optimisation (`NI_LTO=OFF`) | while you work: `scripts/test.sh quick`, and `full --no-coverage --build build-dev` to host and validate what you changed. Its bundles pass every test and pluginval, but are never the ones released |
| `release` | `build/` | universal (arm64 and x86_64), `Release`, link-time optimisation — what ships | the gate: `scripts/test.sh full`, and every bundle that is installed or released |
| `coverage` | `build-coverage/` | `Debug`, one architecture, instrumented | `scripts/coverage.sh` alone, which configures it |

```sh
cmake --preset dev && cmake --build --preset dev        # or: scripts/test.sh quick
cmake --preset release && cmake --build --preset release
```

**Gates between steps run `scripts/test.sh full --no-coverage`**: everything
in the full tier but the coverage stage, which builds the whole tree again
instrumented. Coverage runs once, at the final gate, as plain
`scripts/test.sh full`.

## Build speed

Every C, C++ and Objective-C++ compile goes through **ccache** when it is
installed (`brew install ccache`; `-DNI_USE_CCACHE=OFF` turns it off), and its
hits work across build directories and worktrees: `cmake/NiCcache.cmake` gives
ccache the checkout as `base_dir`, so paths are hashed relative to the build
directory, and maps the build directory to `.` in debug info, so the compile
directory a `-g` build records is the same in every one. A debugger then finds
the sources with `settings set target.source-map . <build dir>` (lldb). The
coverage build keeps its absolute paths, because llvm-cov reads its sources
back through them, and hits only in its own directory. `ccache -s` shows what
the cache did; its default size, 5 GiB, holds the dev and release builds of a
checkout several times over (both together take about 0.6 GiB).

**The Rust side needs nothing more.** Within a checkout, ctest's cargo suites
and a `cargo test` typed at the root already share the workspace's own
`target/`, and each build directory keeps its own cargo target for the
engines. Measured, a shared cache would buy little: sccache 0.18 took a fresh
`cargo test --no-run --workspace` from 18 s to 15 s and the engines' release
build from 17 s to 10 s, because the workspace's own crates compile
incrementally and staticlibs, proc-macros and build scripts are not cached —
not worth a resident server whose path settings are per user rather than per
checkout. A shared `CARGO_TARGET_DIR` across checkouts would rebuild each
checkout's own crates anyway and make every checkout's cargo runs wait on one
lock.

**Neither unity builds nor precompiled headers.** The bulk of a cold build
is JUCE's modules, which are already one file per module, and ccache serves
their repeats: each module is compiled into every plugin and test program
that uses it, with the same flags in most, so two thirds of the compiles of a
cold build with an empty cache are already hits. What is left for either to
win is an edit's recompile, at most the seven seconds below; a unity file
would recompile its neighbours with every edit, and a precompiled header
needs a ccache configuration of its own to be cached at all.

### Measured

On a 10-core M1 Pro (16 GB, Apple clang 21), one run after another, with
Ableton Live and a Docker VM running beside them throughout (load average
35–50), so read the ratios rather than the seconds. "Before" is the old
configuration on today's tree: Unix Makefiles, universal, `Release`, LTO, no
ccache. Each "after" starts from an empty ccache.

| | before | after: `dev` | after: `release` |
|---|---|---|---|
| cold build, everything (configure and build) | 3230 s | 692 s | 1045 s |
| the same in a second new build directory | 3230 s | 127 s (98.8% hits) | 255 s (98.9% hits) |
| rebuild after touching one kit file: everything | 104 s | 11 s | 92 s |
| the same, the test programs only (`--target ni_tests`) | 19 s | 7 s | — |
| quick tier, warm | 28 s | 16 s | — |
| full tier `--no-coverage`, warm | 264 s¹ | 180 s | 182 s |

¹ 102 s of it relinking the plugins after the kit file touched in the row
above; the tests and validators take about the same time in every column,
some 170 s.

"Touching one kit file" is `plugins/_shared/ui/src/Button.cpp`, recompiled
for real (`CCACHE_DISABLE=1`, since an unchanged file is a cache hit): the kit
is compiled into every editor, plugin and test program that uses it, so one
file is 18 compiles and a link per program, each of them twice over and
each plugin's link with link-time optimisation in `release`. The second new build directory is the
case of a new worktree, or a build directory wiped: a checkout elsewhere on
disk hit 97.5% of the same compiles. The cache held 0.6 GiB after all of
this, of its default 5 GiB.

## Quick — the developer loop

`cmake --build build-dev --target ni_tests` builds the test programs and the
engine archives they link, and no plugin bundle; then `ctest -L quick` runs:

- every Rust crate's tests (`cargo test`, crate by crate) — among them the
  no-allocation checks, the two-thread stress runs and the property tests,
  whose seed is fixed so the tier's verdict never changes without a commit —
  and `cargo_deny`, the licence gate over their dependency graph (below),
- the C tests against each engine's generated ABI (`tg_core`, `sc_core`,
  `spectro_columns`, `srecv_api`, `abus_roundtrip`, `gnd_roundtrip`,
  `shell_handoff`), the Trance Gate's ABI held to its last hand-written
  headers (`capi_compat_tg*`), and the oracles that pin each engine's curves,
  envelopes and fades to a table it measured itself
  (`engines/<product>/tests/fixtures`: `tg_curves`, `tg_envelope`, `tg_fade`,
  `sc_shape`, `sc_envelope`),
- the C++ units: the plain helpers every processor shares (`tests/cpp`:
  `ni_wire`), the JUCE shell's state codec over every product's iPlug2
  fixture (`nist_fixtures`), and each product's processor, model and editor in
  one program with its audio callback under an allocation guard (macOS) —
  NI Trance Gate's `tg_processor` and `tg_rt`, and in `tests/<product>`
  NI Spectrogram's (`sg_processor`, `sg_rt`, `spectro_state`,
  `spectro_wire`), NI Listen-In's (`li_processor`, `li_rt`, on a bus
  namespace of its own) and NI Side-Chain's (`sc_processor`, `sc_rt`, its MIDI
  and panic and its key bus included),
- the native kit's and the editors' unit tests (`tests/ui`:
  `ni_ui_tests_*_unit`) — every control, every editor on its fakes, and every
  info line held to 72 characters; their snapshot goldens are in the full
  tier,
- the lint-like checks over the tree: `versions`, `release` (the tag parser,
  release.json and the release staging), `licenses` (the tree, not the
  bundles), `spdx` (every source file opens with its licence and copyright),
  `design_paths` (every path into `design/` that a tracked file names
  exists), the token guards (`ui_tokens_native`: the generated `UvTokens.h`
  against the design system; `site_tokens`: the site's `tokens.css` against
  it, and no colour spelled anywhere else in the site), `ground_shells` (the
  order of the ground's calls in every processor), and `timing` (the timing
  log's helper and its summary).

No bundle, no host, no second process. Warm, it takes under twenty seconds
(Build speed, above).

## Full — final verification

Everything quick runs, and:

| label | what |
|---|---|
| `render` | the render A/B goldens: four seconds through each engine's plugin audio path, hashed against the Move module's reference |
| `host` | every VST3 from `build/out`, hosted by JUCE as a DAW hosts it. `juce_host_*` runs, saves and opens every bundle. NI Trance Gate's (`tg_host`): its iPlug2 class, its parameters through the controller, every iPlug2 fixture reopened as Live reopens a set and saved back byte for byte, the golden render through its audio path, the host's Bypass passing the audio through bit for bit, and the window under a running transport with a set loaded on another thread. NI Spectrogram's (`sg_host`) the same, its audio through bit for bit; NI Listen-In's (`li_host`) the same, its audio read back off the bus, a reopened set's bus claimed under its name, and nothing published while bypassed; NI Side-Chain's (`sc_host`) the same, the engine's golden render through it, a note, CC 120 and CC 123 through the host's event list and MIDI-CC mapping, the Bypass passing audio while the engine still hears the notes, and a key on its sidechain bus |
| `snapshot` | the kit's and every editor's snapshot goldens (`tests/ui/baselines`), drawn with JUCE's `createComponentSnapshot`; they are macOS renders |
| `ipc` | the bus written in one process and read in another — and, on an arm64 Mac with Rosetta, between the x86_64 and arm64 slices both ways round |
| `bundles` | every built bundle carries its notices (`licenses_bundles`); every bundle's signature verifies as Live's scanner checks it (`codesign_*`: `codesign --verify --deep --strict`); the bundles' version spellings (`versions_bundles`); the release staging, dry-run against the bundles (`release_bundles`) |
| `site` | every root-relative link on the built site resolves |
| `coverage` | the coverage floor, in the instrumented build |

`scripts/test.sh full` builds everything and the site, runs `ctest -L full`,
then `scripts/coverage.sh` with the floor enforced (skipped with
`--no-coverage`, for the gates between steps), then the validators over the
bundles in `build/out` (or `--bundles DIR`).

**The validators** (`scripts/validate-plugins.sh`, VST3 only) are the hosts'
own rules rather than ours: `pluginval` at strictness 10 **with its editor
tests**, Steinberg's VST3 `validator` built from the SDK at the tag JUCE
vendors, and on macOS `codesign --verify --deep --strict` again, on exactly
the bundles validated. pluginval is pinned to one release with a SHA-256 per
platform, and the script runs on macOS, Linux (under `xvfb-run` without a
display) and Windows (Git Bash). Nothing they read is installed: every bundle
is validated where the build left it.

**The iPlug2 fixtures** in `tests/fixtures/iplug2` are what the last iPlug2
builds (v2026.10.06.5) saved, captured through the calls Live makes
([README](../../tests/fixtures/iplug2/README.md),
[FORMAT](../../tests/fixtures/iplug2/FORMAT.md)). Every product's processor
test loads each to its exact parameters, `nist_fixtures` writes each back
byte for byte, and each host test reopens each in the built VST3. They cannot
be captured again from this tree: the README names the commit that can.

**Windows and Linux.** The full tier runs on macOS. The cross-build kit
(`scripts/build-all.sh`, [cross-build](cross-build.md)) builds the plugins
for Linux in Docker and for Windows with clang-cl, and runs their ctest and
pluginval there — Windows under Wine, which is an approximation, labelled as
such. The first real Windows and Linux runs are the release's
(`.github/workflows/release-plugins.yml`): the release build on
`windows-2025` and `ubuntu-24.04`, the quick tier and the host tests, and the
validators, before anything is published.

## Licences

The project is GPL-3.0-or-later
([docs/adr/0001-gpl-3.0-or-later.md](../adr/0001-gpl-3.0-or-later.md)), and
three quick-tier tests hold it to that:

| test | what it holds |
|---|---|
| `spdx` | every source file this repository owns opens with `SPDX-License-Identifier: GPL-3.0-or-later` and `Copyright (C) 2026 Torben Gräber`, in its own comment syntax. `tests/spdx.test.mjs` lists what is excluded and why: external code, the design mirrors, Schwung's vendored headers |
| `cargo_deny` | `cargo deny check licenses bans sources`: every crate in the graph is under a licence on `deny.toml`'s allowlist and comes from crates.io, and a crate in two versions is shown |
| `licenses` | `LICENSE` is the unmodified GPLv3; `THIRD_PARTY_LICENSES.md` lists exactly what ships — JUCE at the submodule's version and the VST3 SDK at the version of JUCE's copy, what JUCE compiles in, the fonts, the engines, and exactly the crates and versions `cargo metadata` says ship; and every C and C++ library's licence is on the allowlist (AGPL-3.0 for JUCE alone) |

The allowlist is `deny.toml`'s, the one policy: MIT, Apache-2.0, BSD-1/2/3,
ISC, Zlib, MPL-2.0, Unicode-3.0, CC0, Unlicense, GPL-3.0, and libpng-2.0, IJG
and MIT-Modern-Variant for the libraries JUCE compiles into every bundle.
`about.toml` accepts the same list, and `licenses` holds the two equal.

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
carries `full` and a label saying why — `ctest -L host` runs one kind alone. A
test registered in no tier stops the configure: a new test is placed on
purpose, never by default.

## The timing log

Every build and test entry point logs how long each of its stages took:
`scripts/test.sh` (each stage, and the whole run as `total`),
`scripts/coverage.sh`, `scripts/validate-plugins.sh` (each validator on each
bundle), the cross-build scripts (`build-macos.sh`, `build-linux.sh`,
`build-windows.sh` — inside their containers too — and each platform of
`build-all.sh`) and `modules/_shared/package.sh`. They all do it through one
helper, `scripts/timing.sh`:

```sh
. scripts/timing.sh
ni_time_stage build --build build-dev -- cmake --build build-dev
```

It runs the command unchanged — its output, its exit status, `set -e` as the
caller had it — and appends one JSON line to a log **outside every
checkout**, so removing a worktree or a build directory never removes it:

| | |
|---|---|
| macOS | `~/Library/Logs/neon-ingvy/build-timings.jsonl` |
| Linux, elsewhere | `$XDG_STATE_HOME/neon-ingvy/build-timings.jsonl` (`~/.local/state` by default) |
| any | `NI_TIMING_LOG=<file>` instead |

A record holds `time` (the start, UTC), `run` (one id for every stage of one
invocation, the scripts it calls included), `sha`, `branch`, `worktree`,
`script`, `stage`, `preset`, `build_type`, `generator`, `archs`, `duration_s`,
`exit_code`, `ccache_hits` and `ccache_misses` (what `ccache --print-stats`
counted during the stage — the machine's counters, so a build running beside
it counts too), `cpus`, `load1` (the one-minute load average at the start) and
`host`. A build stage also leaves its build directory's `.ninja_log` beside
the log, as `ninja/<time>-<script>-<stage>.ninja_log`, for per-target
analysis (`ninja -t`, or ninjatracing for a Chrome trace).

```sh
scripts/build-timings.py                          # per stage, per day, the slowest, ccache
scripts/build-timings.py --since 2026-10-08 --stage ctest
scripts/build-timings.py --json                   # the same, for a program
```

It reads the log with Python's standard library alone: per stage (by script)
the count, median, 90th percentile and last duration and the failures; per
day the stages, their total time and failures; the slowest single stages
with their commit and checkout; and ccache's hit rate. `--since`, `--stage`
and `--script` filter. The quick tier's `timing` test runs the helper and the
summary into a log of their own.

## Coverage

`scripts/coverage.sh` measures the Rust and our C and C++ — the engines, the
JUCE shell, the native kit, every editor and every processor, in the test
programs and inside the bundles the host tests load — into one report in
`build/coverage/` (`coverage.json` for a program, `summary.txt` and
`html/index.html` for a person, `lcov.info` for an editor's gutter). JUCE and
the other submodules are not counted. It builds instrumented into
`build-coverage/` with the `coverage` preset, so nothing instrumented can be
shipped.

`tests/coverage.floors.json` holds the **80 % floor**, and it is enforcing:
a unit below it, a total below it, or a first-party file no test loads fails
`coverage_floor` and the script. Units are read off the path — each engine
crate, each plugin directory (`plugins/_shared` is one), each module — so a
new crate or plugin is judged from its first file. An exemption is for what
cannot be built into anything a test runs, never for what is merely
untested, and each carries its reason, audited for a reason and a path that
still exists:

| exempt | why |
|---|---|
| `modules/trance-gate` (unit) | `ui_chain.js` runs on the Move inside Schwung's shadow UI and imports Schwung's own modules by their paths on the device, which this repository does not carry; `engines/trance-gate/tests/smoke_ui.mjs` runs every entry point in the module's own build. Its DSP and C shell are counted |
| `plugins/_shared/ui/src/ReducedMotion.cpp` (file) | every line is outside macOS (`#if ! JUCE_MAC`), and coverage runs on macOS; `ReducedMotion.mm` is counted |
| `plugins/_shared/ui/gallery/Main.cpp` (file) | the gallery application's entry point, a window for a person to look at; every page it shows is counted through the kit's tests |

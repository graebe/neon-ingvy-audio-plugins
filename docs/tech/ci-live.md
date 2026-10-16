---
title: Build & CI — Ableton Live
order: 4
slug: ci-live
---

## Locally

Everything is built and tested on the developer's machine first; GitHub Actions
only publish. The presets in `CMakePresets.json` are the builds:

| preset | directory | what |
|---|---|---|
| `dev` | `build-dev/` | this machine's architecture, RelWithDebInfo, no LTO: the loop to iterate in |
| `release` | `build/` | Release with LTO, universal on macOS: what ships |
| `coverage` | `build-coverage/` | instrumented for llvm-cov, never shipped |

**macOS**, natively:

```sh
git submodule update --init --recursive   # JUCE 9. The engines are subtrees.
cmake --preset release                    # universal, LTO: what ships (build/)
cmake --build build                       # every VST3, into build/out
scripts/test.sh quick                     # the developer loop (build-dev/)
scripts/test.sh full                      # everything, the validators included
scripts/test.sh product side-chain        # one product, as its release runs it
```

**Developer and standard builds are macOS only**, and so are the releases.
Linux and Windows are not released: the cross-build kit (see **Cross-platform
builds**) stays in the repository, and they rejoin later, then only as part of
the full build.

Artefacts land in `build/out/`, where every test and validator loads them by
path. No build copies a bundle into the system's VST3 folder unless it is
configured with `-DNI_DEPLOY_PLUGINS=ON`, so a build never replaces what a host
has installed.

**`-DNI_SANITIZE=thread`** (or `address`, `undefined`) instruments every C and
C++ test — never a plugin. It needs one slice:

```sh
cmake -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DNI_SANITIZE=thread -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-tsan && ctest --test-dir build-tsan -L full -LE host
```

**Needs cargo.** If it is installed and not found, the error names where it
looked — `cmake/RustToolchain.cmake` searches every layout rustup.rs, Homebrew
and a bare toolchain use, because Homebrew's keeps its shims in
`/opt/homebrew/opt/rustup/bin`, which is not `~/.cargo/bin`.

## CI

`.github/workflows/ci.yml` runs **by hand only**. Every change is built and
tested locally before it is pushed (`scripts/test.sh quick` and `full`); that
is the gate, and Actions publish rather than discover. Started by hand, it
checks a clean runner on a pinned `macos-15` image, with a newer run cancelling
the older one. Three jobs: **quick** and **full** run `scripts/test.sh` in each
tier, and **tsan** builds the tests under ThreadSanitizer and runs them.

**The suite is the asset here**, and it is not a smoke test:

| | |
|---|---|
| `tg_render_ab` | four seconds through the plugin's audio path, hashed against the Move module's reference render. **The check that a refactor did not change the sound.** |
| `tg_curves`, `tg_envelope` | the editor's envelope maths against the engine's own *measured* output — the engine is run with a DC input at amount 1, where the gain it applies **is** the envelope |
| `ui_tokens_native` | the kit's generated tokens agree with the vendored design system, and no colour is spelled anywhere else |
| `versions` | every spelling of a product's version agrees with `versions.json`: the build, the bundle's plist and moduleinfo.json, module.json and every crate |
| `release` | a tag means what the release workflows think it means |
| `licenses` | everything that ships has a notice, and every bundle carries them (`licenses_bundle_*`) |
| `spectro_core` | the FFT against a naive DFT, the band mapping, and `assert_no_alloc`'s guard proving the audio path allocates nothing |

The JS suites skip rather than fail when node is absent: a C++ developer building
the plugin should not need a JS toolchain to run the C tests.

## Releasing

`trance-gate-v2026.09.29.3` — **one product per tag, the prefix says which, and
the rest is the version exactly as `versions.json` spells it.** A bare `v*`
namespace would make a Spectrogram tag publish a Trance Gate, which is the kind
of mistake only ever noticed by whoever downloads the wrong thing. The tag is
parsed by `scripts/release.mjs`, which both release workflows use and
`ctest -R release` tests.

### The order of a release

```sh
git status                                # clean: nothing uncommitted, nothing untracked
scripts/test.sh product side-chain        # or scripts/test.sh full -- on the commit to be tagged
git push origin main
git push origin refs/notes/ni-tests       # the proof that run passed, BEFORE the tag
git tag side-chain-v2026.10.10.2
git push origin side-chain-v2026.10.10.2
```

**When main has moved by a catalog commit only.** A module release commits
`Schwung catalog: <module> <version>` to main, so `git push origin main` can
be refused after a test run that took an hour. Do not rebase onto it: that
makes a new commit, and the note is on the old one. Merge it, and tag the
commit you tested:

```sh
tested=$(git rev-parse HEAD)
git pull --no-rebase origin main          # brings in release.json, nothing else
git push origin main
git push origin refs/notes/ni-tests
git tag side-chain-v2026.10.10.2 "$tested"
git push origin side-chain-v2026.10.10.2
```

That is safe because the tagged tree's `release.json` is never what gets
published. The catalog step writes the new entry onto **main's** `release.json`,
whatever main has gained since. The beta check reads the tagged tree's copy,
and an older copy is only wrong if it predates a release of the **same**
module, which a catalog commit for another module cannot be. If main gained
anything else, the commit you tested is not the one main will hold; test the
merge, and tag that.

**A release proves its tests.** The owner's rule is that everything is built
and tested locally and GitHub Actions publish, and a workflow cannot see a
local run — so the run leaves its proof on the commit. When
`scripts/test.sh full` (with coverage) or `scripts/test.sh product <product>`
passes on a clean tree, it appends one JSON line to a git note on `HEAD`
under its own ref, `refs/notes/ni-tests`:

```json
{"tier":"product","products":["side-chain"],"sha":"…","date":"…","host":"…",
 "rustc":"rustc 1.99.0 (…)","result":"pass",
 "results":{"duration_s":1650,"build_type":"Release","archs":"arm64;x86_64",
            "gaps":0,"waived":"docker","untested":"docker"}}
```

`git notes --ref=ni-tests show HEAD` prints them. Nothing is recorded for a
tree with uncommitted or untracked changes (at the start or the end of the
run), a `HEAD` that moved during it, `full --no-coverage`, or a run on a
GitHub runner; the run says so in its last lines and stays green. Both release
workflows fetch the notes first and stop, **before they build anything**,
unless the tagged commit carries a passing `product` run of the tag's product
or a `full` run (`scripts/release.mjs attested`), **with no gap left unwaived**.
`gaps` counts the tests that could not run on that machine, Docker's or
Rosetta's say, less the ones waived by name with `--waive` (see
[Testing](testing.md)). A product run passes with gaps, but it does not release
with them: run it where the capability exists, or waive the gap so it is on the
record. A note from before gaps were recorded does not count. There is no
override: a commit that has not been tested is tested, then tagged.

If a release stops here, push the missing note and **re-run the failed jobs**
in the Actions tab. The tag is already on GitHub, so pushing it again starts
nothing.

The notes push is refused only if notes were recorded on two machines. Join
them, then push again — each line is one run, so concatenating is the whole
merge:

```sh
git fetch origin refs/notes/ni-tests:refs/notes/ni-tests-origin
git notes --ref=ni-tests merge -s cat_sort_uniq refs/notes/ni-tests-origin
git push origin refs/notes/ni-tests
```

### What the workflow does

`release-plugins.yml` checks the tag against the tree, asks for the tested-run
note above, then checks the supply chain — `cargo deny check advisories
licenses bans sources`, RustSec's advisories as of the release day included —
and runs `scripts/test.sh product <product>` on macOS — the same command a
developer runs locally ([Testing](testing.md)). It builds **that product alone**: its
VST3 and the test programs of it and of the shared code, never another
product's. It runs that product's tests and the shared ones of both tiers (a
tag is the worst possible moment to discover the render A/B moved), then the
validators over its one bundle — pluginval at strictness 10 with its editor
tests, Steinberg's VST3 validator and `codesign --verify --deep --strict`. It
stages the bundle with `LICENSE` and `THIRD_PARTY_LICENSES.md` beside it, and
packs `<product>-<version>-macOS.zip`. Beside the zip the release carries
`<product>-<version>-macOS-tests.md`: the run's own summary of what it tested
and what it did not ([Testing](testing.md), What a run did not test), so what
a published build was tested with stays on the record.
Linux and Windows are not released for now; they rejoin as part of the full
build.

**One product's failing test holds that product's release and no other.**
The job used to build everything and run the whole full tier, one to two
hours on GitHub's runner, so a race in one product's state test held up the
release of every product. A failing shared test still holds every release,
because what it checks ships in all of them. The full tier remains the gate
before anything is tagged.

Its last job redeploys this site (**Deploying this site**, below).

Every action in every workflow is pinned by its full commit SHA, with the
release it is beside it (`# v4.4.0`): a tag is a pointer its owner can move,
a commit is the code that was reviewed. The Rust toolchain is pinned by
`rust-toolchain.toml` (1.99.0, both Apple targets, llvm-tools), which the
runners install with `rustup toolchain install` and the Move module's image
installs too; `ctest -R release` holds all of it.

### Betas

`v2026.10.10.2-beta.1` is a beta **of** `v2026.10.10.2`: it orders below it
and above everything before it. Every spelling is derived as for a release
(`tests/versions.test.mjs`), with one limit: CFBundleShortVersionString and
CFBundleVersion are three integers each, with no room for a prerelease, so a
beta and the release it precedes share them, and only the version string the
editor shows says "beta". Cargo's spelling is `2026.10.10-beta.1+2`.

The tag is `side-chain-v2026.10.10.2-beta.1`, and versions.json must say the
same. It is published as a GitHub **prerelease** (never **Latest**), the site
never links it, and a Move module's beta goes to its `beta` channel
([Build & CI — Schwung](ci-schwung.md)).

### Seven releases a day

CFBundleVersion is `y.m.(day*8+n)`: the subversion lives in the last field's
three low bits, so `n` runs `.1` to `.7` — **at most seven releases of a
product a day**. An eighth would wrap into the next day's number and read as
older than it is, so configure refuses it (`cmake/NiJucePlugin.cmake`) and
`ctest -R versions` does too. Release it as the next day's `.1`.

## Rolling back

A release that should not have shipped is withdrawn, then **fixed forward**:
a version number is never reused, because a host or a Move that installed it
cannot tell two builds with one number apart.

1. **The VST3.** On GitHub, edit the release and either mark it a
   **prerelease** (it stops being **Latest** and leaves the site's links at the
   next deploy) or delete it (the zip's URL then 404s for anyone holding the
   link). Keep the tag: it is the source the GPL promised for every copy
   already downloaded. `gh release edit <tag> --prerelease` or
   `gh release delete <tag>` (the tag stays unless `--cleanup-tag` is given).
2. **The Move module**, if the tag shipped one: revert its `release.json`
   entry on `main` ([Build & CI — Schwung](ci-schwung.md), **Rolling back**).
3. **The site.** Run **Pages** by hand (Actions → Pages → Run workflow, or
   `gh workflow run pages.yml`): it links the newest published release that
   is not a prerelease, so the withdrawn one disappears.
4. **Fix forward**: the fix, the full tier, a new subversion
   (`v2026.10.10.3`), its note, its tag — as any release.

## Signing and notarisation

Gated on six repository secrets, and skipped cleanly without them:

| secret | |
|---|---|
| `MACOS_CERTIFICATE_P12` | base64 of the Developer ID Application certificate and key, as a `.p12` |
| `MACOS_CERTIFICATE_PASSWORD` | that `.p12`'s password |
| `MACOS_SIGNING_IDENTITY` | e.g. `Developer ID Application: Torben Gräber (TEAMID)` |
| `NOTARY_APPLE_ID`, `NOTARY_TEAM_ID`, `NOTARY_PASSWORD` | the Apple ID, team and an app-specific password for `notarytool` |

With them, the staged macOS bundle is signed with `codesign --options runtime
--timestamp` *after* its notices are inside it (a signature seals every file in
the bundle), submitted with `notarytool --wait`, and stapled. The certificate lives in a throwaway keychain that is deleted at the
end of the job. Without them the bundle ships with the ad hoc signature the
build gave it, and the release notes carry the one command that gets macOS to
load it:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/NITranceGate.vst3
```

## Deploying this site

`.github/workflows/pages.yml` builds `site/` from `main` and deploys it to
GitHub Pages. Both release workflows **call it as their last job**, after the
release is published, and it runs by hand (`workflow_dispatch`). It used to
listen for `release: published` and never once ran for one: the releases are
published with the default `GITHUB_TOKEN`, and GitHub starts no workflow from
an event that token caused. One deploy runs at a time, in the order they
started (a tag with a plugin and a module calls it twice).

The download links are the **published releases**, not `versions.json`:
each product's newest release on GitHub that is not a prerelease, a beta, or
ahead of `versions.json` (`gh release list`, read by
`scripts/release.mjs latestStableRelease`). A bump that is not tagged yet, a
tag whose release failed, and a beta are never linked. A local build of the
site uses the checkout's tags instead.

**Two settings, once:**

- Settings → Pages → Source: **GitHub Actions**. Until it is, the deploy step
  fails with a message that does not obviously say so.
- Settings → Environments → `github-pages` → Deployment branches and tags:
  besides `main`, **allow the release tags** (`*-v*`). A deploy called from a
  release runs on the tag's ref, and the environment refuses any ref it does
  not list. Or:
  `gh api -X POST repos/graebe/neon-ingvy-audio-plugins/environments/github-pages/deployment-branch-policies -f name='*-v*' -f type=tag`

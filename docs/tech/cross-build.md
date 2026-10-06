---
title: Cross-platform builds
order: 7
slug: cross-build
---

Every plugin is built and checked for all three desktop platforms on one Mac,
before anything reaches GitHub: macOS natively, Linux and Windows in Docker.
GitHub Actions only publishes. One command builds a JUCE plugin project for
each platform, runs its tests, validates every VST3 it produced with
pluginval, and sums up:

```sh
scripts/build-all.sh --juce <JUCE 9.0.3> tools/cross/smoke
```

`tools/cross/smoke` is the proof the kit is held to: a JUCE 9.0.3 VST3 whose
`processBlock` hands every channel to a twenty-line Rust static library through
a C ABI — the shape every product here has. It ships nowhere.

## What runs where

| | macOS | Linux | Windows |
|---|---|---|---|
| Script | `scripts/build-macos.sh` | `scripts/build-linux.sh` | `scripts/build-windows.sh` |
| Where | natively | `tools/docker/linux`, linux/amd64 | `tools/docker/windows`, linux/amd64 |
| C++ | Apple clang (Xcode) | clang 20 | clang-cl 20 and lld-link, against the MSVC 14.44 C runtime and Windows SDK 10.0.26100 |
| Rust | `aarch64-` and `x86_64-apple-darwin`, joined with lipo | `x86_64-unknown-linux-gnu` | `x86_64-pc-windows-msvc`, static C runtime |
| Artefact | universal `.vst3` | `.vst3` holding `x86_64-linux/*.so` | `.vst3` holding `x86_64-win/*.vst3` |
| ctest | natively | in the container | under Wine |
| pluginval 1.0.4 | strictness 10, editor tests included | strictness 10, editor tests under Xvfb | `pluginval.exe` under Wine and Xvfb: a smoke test |
| Build directory | `<project>/build-macos-universal` | `<project>/build-linux-amd64` | `<project>/build-windows-x64` |

## Prerequisites

- **macOS:** Xcode's command-line tools, CMake 3.22 or later, Ninja, and Rust
  with both Apple targets (`rustup target add aarch64-apple-darwin
  x86_64-apple-darwin`) — what the plugins already need.
- **Docker Desktop**, with *Use Rosetta for x86_64/amd64 emulation* on Apple
  silicon (both images are linux/amd64), at least 8 GB of memory for its VM,
  and about 6 GB of disk for the two images.
- **JUCE at tag 9.0.3**, passed as `--juce <dir>`. The repository will keep it
  as a submodule at `external/JUCE`, which is the default; until then:
  `git clone --depth 1 --branch 9.0.3 https://github.com/juce-framework/JUCE.git <dir>`.
- **For Windows, the owner's acceptance of Microsoft's licence** for the C
  runtime and the Windows SDK — see below. Without it the Windows build stops
  before anything of Microsoft's is downloaded.

## Running it

```sh
scripts/build-all.sh --juce <dir> <project>         # all three, then the summary
scripts/build-macos.sh --juce <dir> <project>
scripts/build-linux.sh [--arch arm64] --juce <dir> <project>
XWIN_ACCEPT_LICENSE=yes scripts/build-windows.sh --juce <dir> <project>
```

`<project>` is any CMake project inside the repository that takes its JUCE
from `-DNI_JUCE_DIR` — the smoke plugin now, the repository itself once it has
moved to JUCE. Each script configures `<project>/build-<platform>` (Release,
Ninja), builds, runs `ctest`, then runs pluginval on every `.vst3` under the
build directory and exits non-zero if any of that failed, having run all of
it. `build-all.sh` runs the three in turn and ends with a table:

```text
| Platform | Artefact | Size | ctest | pluginval | Wall time |
```

Each platform leaves its full output in `cross-build.log` and its per-stage
timings in `cross-result.tsv`, both in its build directory; every pluginval
run has its own `pluginval-<name>.log` beside them.

## How it works

### One contract, three scripts

`scripts/cross-common.sh` holds what the three share: the arguments, the
configure-build-test sequence, pluginval over every bundle, the result file and
the image handling. The Linux and Windows scripts run twice: on the host, where
they make sure the image exists and start a container, and inside it, where
they build. The container runs **as the invoking user** with the repository at
`/work` and JUCE at `/juce` (read-only), and everything it writes — objects,
`CARGO_HOME`, `HOME` with its caches, the Wine prefix — lands in the build
directory, never in the image. Nothing is installed anywhere: pluginval loads
each bundle where the build left it, and nothing is copied into
`~/Library`.

### Images: pinned, and built only when they change

An image is tagged with a digest of its Dockerfile (and build arguments), so a
missing tag means a changed Dockerfile: a moved pin builds a new image on the
next run, and an unchanged one costs a `docker image inspect`. Everything in
them is pinned:

| What | Pin | Where |
|---|---|---|
| Base | `ubuntu:24.04` by index digest | `tools/docker/linux/Dockerfile` |
| Every Ubuntu package (clang, CMake, Ninja, the -dev packages, Xvfb, Wine) | one archive snapshot, `20261001T000000Z`, from `snapshot.ubuntu.com` | the same |
| LLVM | 20 (20.1.2 in that snapshot) | the same, `LLVM_MAJOR` |
| Rust | rustup-init 1.29.1 by SHA-256, toolchain 1.98.1 | the same, and `modules/_shared/Dockerfile`, which move together |
| pluginval | 1.0.4 by SHA-256, per platform | the Linux and Windows Dockerfiles; `scripts/validate-plugins.sh` for macOS |
| xwin | 0.10.0 by SHA-256 | `tools/docker/windows/Dockerfile` |
| MSVC C runtime, Windows SDK | 14.44.17.14 and 10.0.26100, from the Visual Studio 2022 channel | the same |

A snapshot rather than per-package versions, because Ubuntu removes a version
from its archive as soon as it supersedes it, and a snapshot never changes.
The one package taken from the live archive is `ca-certificates`: apt needs it
to reach the snapshot service at all.

### Linux

The packages are those JUCE 9.0.3's `docs/Linux Dependencies.md` asks for, for
a plugin with `JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0`: FreeType and
Fontconfig (juce_graphics), X11 with Xcursor, Xext, Xinerama, Xrandr and — new
in JUCE 9 — Xi (juce_gui_basics), and ALSA (juce_audio_devices, for a plugin
that links juce_audio_utils). No WebKit, no curl, no GL: only juce_opengl needs
it, and no plugin here uses it. Xrender and Xcomposite are not needed either:
JUCE only includes them with `JUCE_USE_XRENDER`, which is off by default.
pluginval runs under `xvfb-run`, so its editor tests open the editor for real.

`--arch arm64` builds the same image for linux/arm64 and the project in it,
natively on Apple silicon. pluginval publishes no arm64 Linux build, so those
bundles are reported as *not validated*, never as passed.

### Windows

The image has two stages. **`tools`** adds, to the Linux image, the rest of
the same LLVM (clang-cl, lld-link, llvm-lib, llvm-rc, llvm-mt), Rust's
`x86_64-pc-windows-msvc` standard library, Wine 9.0 (64-bit only) and
`pluginval.exe`; it holds only open-source tools. **`sdk`** adds the MSVC C
runtime and the Windows SDK, downloaded by xwin — and only with the owner's
`XWIN_ACCEPT_LICENSE=yes`.

`tools/cross/windows-clang-cl.cmake` is the toolchain file: it names the
compilers and tools, adds the CRT and SDK headers with `/imsvc` (so their
warnings are not ours) and their libraries with `/libpath:`, sets
`-fms-compatibility-version` to the CRT's compiler (19.44), and keeps CMake's
library and header searches inside the splat. The Rust side needs nothing for
a static library — nothing is linked — but cargo is configured for the target
through the image's environment (`CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_*`:
lld-link, the splat's library paths, Wine as the runner), so `cargo test` for
Windows links and runs too. None of that is in a committed
`.cargo/config.toml`: it means nothing outside the image.

Two things differ from a native Windows build. JUCE writes a VST3's
`moduleinfo.json` by compiling a helper with the plugin's compiler and running
it on the build machine; a cross-compiled helper is a Windows program, so the
cross build turns that step off (`VST3_AUTO_MANIFEST`) and hosts scan the
plugin instead. And ctest's Windows executables run under Wine, through
`CMAKE_CROSSCOMPILING_EMULATOR`.

### Rust in every build

`tools/cross/RustStaticLib.cmake` builds a crate for whichever platform the C++
build targets — never the machine cargo runs on — and hands it over as an
interface library: one slice per macOS architecture joined with lipo, the host
triple on Linux, the MSVC triple on Windows. On Windows the C runtime follows
the C++: a project on the static runtime (`CMAKE_MSVC_RUNTIME_LIBRARY` without
`DLL`, as the smoke plugin and every plugin here) gets Rust built with
`+crt-static`, or the link would pull in both runtimes. cargo's target
directory is the build directory's own, so the three platforms' builds of one
checkout never share objects. `ni_add_rust_test` adds the crate's `cargo test`,
for the same target, to ctest.

## The Microsoft licence

The Windows image's `sdk` stage downloads Microsoft's C runtime and Windows
SDK, which are not free software and are not in this repository.
`tools/docker/windows/README.md` says what xwin asks to be accepted, where the
licence terms are, and how the acceptance is given: as
`XWIN_ACCEPT_LICENSE=yes`, by the owner, once per image build. No script or
agent gives it on the owner's behalf. The image built that way is **local
only**: it is never pushed to a registry.

## What the Wine run covers, and what it does not

`pluginval.exe` under Wine loads the real Windows DLL and drives it through
the same strictness-10 suite as the other platforms: scanning and
instantiation, audio at every sample rate and block size, state save and
restore, automation and parameter fuzzing, bus layouts — and, with Xvfb, the
editor tests. A pass means the binary loads and behaves through Wine's
implementation of Windows, under x86_64 emulation.

It is not Windows. Wine's Direct2D, DirectWrite and window management are its
own, so a drawing or DPI fault on real Windows can pass here and a Wine
shortcoming can fail here; there is no real audio device, no Windows host, and
no code signing. The run that counts is the native Windows one in GitHub
Actions at publishing time.

## Timings

Measured on 2026-10-06 on this machine (Apple silicon, Docker Desktop with 10
CPUs, 8 GB and Rosetta), on the smoke plugin. *Cold* is a first build: images
built, JUCE compiled from scratch.

| Step | macOS | Linux amd64 | Windows x64 |
|---|---|---|---|
| Image, first build | — | 4 min 34 s | not built (licence) |
| Configure | 10 s | 58 s | — |
| Build, cold | 3 min 00 s | 7 min 53 s | — |
| ctest | 2 s | 15 s | — |
| pluginval, strictness 10 | 18 s | 21 s | — |

Linux builds under x86_64 emulation, at one compile job per GiB of the Docker
VM's memory (7 here): JUCE's module translation units are large, and more jobs
than memory allows end in the out-of-memory killer. `CMAKE_BUILD_PARALLEL_LEVEL`
overrides it.

## Known limits

- **The Windows build is unproven until the owner accepts Microsoft's
  licence.** Everything before that gate is built and checked — the `tools`
  stage, the Rust static library for `x86_64-pc-windows-msvc`, `pluginval.exe`
  starting under Wine — but compiling and linking JUCE with clang-cl, and the
  Wine pluginval run on the result, wait for that first run.
- **The Visual Studio channel manifest is read live.** The CRT toolset and the
  SDK line are pinned, and xwin checks every download against the manifest's
  digests, but Microsoft may service a pinned SDK under the same version, and
  will eventually drop an old toolset from the channel; that build then fails
  and names the version.
- **No `moduleinfo.json` in a cross-compiled Windows bundle** (above).
- **No validator for linux/arm64**: pluginval publishes no build for it.
- **Debug builds on Windows** link Rust's release-CRT objects into a debug-CRT
  C++ build; the kit builds Release.

## Cleaning up

Build directories are `build-macos-*/`, `build-linux-*/` and `build-windows-*/`
inside the project, ignored by git; delete them freely. Images are
`ni-cross-linux:*` and `ni-cross-windows:*`, one per Dockerfile version:

```sh
docker images 'ni-cross-*'
docker image rm <image>            # an old version
docker builder prune -a            # the build cache too (every project's)
```

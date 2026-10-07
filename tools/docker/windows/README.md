# The Windows cross-build image, and the licence it needs

`scripts/build-windows.sh` cross-compiles the plugins for Windows x64 inside
this image: clang-cl and lld-link compile and link against Microsoft's C
runtime and Windows SDK, and Wine runs the tests and `pluginval.exe`.
[docs/tech/cross-build.md](../../../docs/tech/cross-build.md) is the
walk-through; this page is about the one part that is not open source.

## What needs accepting, and who accepts it

A Windows binary of this kind is compiled against Microsoft's headers and
linked against Microsoft's libraries: the MSVC C/C++ runtime (the "CRT") and
the Windows SDK. They are not free software, and they are not in this
repository or in any public image. The Dockerfile's `sdk` stage downloads
them with [xwin](https://github.com/Jake-Shadle/xwin) (MIT OR Apache-2.0)
from Microsoft's own Visual Studio 2022 release channel, pinned to:

| Package | Pin |
|---|---|
| MSVC C/C++ runtime (headers, static and import libraries) | toolset 14.44 (`14.44.17.14`) |
| Windows SDK (headers, import libraries) | `10.0.26100` |
| Visual Studio channel manifest | version 17 (Visual Studio 2022), read live; xwin checks every download against the SHA-256 the manifest gives |

**xwin will not download them until their licence is accepted**, and the
acceptance is yours to give: no script, CI job or agent in this repository
gives it on your behalf. xwin presents the licence as
<https://go.microsoft.com/fwlink/?LinkId=2086102>, which today resolves to
Microsoft's licence terms for *Visual Studio 2019 Remote Debugger,
Stand-alone Profiler, IntelliTrace, Snapshot Debugger, other Debuggers and
Build Tools* (<https://visualstudio.microsoft.com/license-terms/mlt031519/>);
the Windows SDK also carries Microsoft's own SDK licence terms. Read them
before accepting -- they say who may use the Build Tools and for what, and
nothing here is legal advice on how they apply to you.

## Accepting

The acceptance is the build argument `XWIN_ACCEPT_LICENSE=yes`, which the
build script passes on when you set it in its environment:

    XWIN_ACCEPT_LICENSE=yes scripts/build-windows.sh --juce <JUCE 9.0.3> tools/cross/smoke

It is asked for only while the image is being built: once, and again whenever
this Dockerfile or the Linux one changes (the image's tag is a digest of both).
Later runs use the built image and do not ask. Without it the `sdk` stage
stops before anything is downloaded, and the build script says why.

## Keep the image local

The built image -- `ni-cross-windows:<digest>-amd64` -- contains the CRT and
the SDK. **It is for this machine only: never push it to a registry**, and
never base a published image on it. Only its first stage, `tools`, is free of
Microsoft's files (`docker build --target tools ...`); that one could be
shared.

The same goes for Docker's build cache, which keeps the `sdk` layer too. To
remove both (the second command clears every project's unused build cache,
not only this one's):

    docker image rm $(docker images -q ni-cross-windows)
    docker builder prune -a

## What reaches a plugin

The plugins link the **static** C runtime (`CMAKE_MSVC_RUNTIME_LIBRARY` is
`MultiThreaded`), so a Windows plugin carries Microsoft runtime code inside
its DLL, as any statically linked MSVC program does; whether and how you may
distribute such a binary is again a question for those licence terms. The
release builds are not these: they come from the native Windows runner in
GitHub Actions. The cross build is for building and checking locally.

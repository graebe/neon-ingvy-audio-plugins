# syntax=docker/dockerfile:1
# check=skip=InvalidDefaultArgInFrom

# THE WINDOWS CROSS-BUILD IMAGE: the Linux image (tools/docker/linux) plus
# clang-cl and lld-link of the same LLVM, Rust's x86_64-pc-windows-msvc target,
# Wine with pluginval.exe -- and, in its second stage, the MSVC C runtime and
# the Windows SDK, fetched by xwin. scripts/build-windows.sh builds it and runs
# every Windows build in it; tools/cross/windows-clang-cl.cmake is the CMake
# side. linux/amd64 only: Wine and pluginval.exe are x86_64.
#
# TWO STAGES, AND ONLY THE FIRST COULD EVER LEAVE THIS MACHINE.
#
#   tools  open-source tools only
#   sdk    tools + Microsoft's CRT and SDK, under Microsoft's licence. It builds
#          only with XWIN_ACCEPT_LICENSE=yes, which the owner sets, and it is
#          a LOCAL image: never pushed to any registry. README.md beside this
#          file says what accepting means and where the licence is.
#
# PINS, on top of the base image's (its Ubuntu snapshot, its LLVM_MAJOR, its
# Rust):
#
#   Wine        wine64 9.0, from the base image's Ubuntu snapshot
#   pluginval   the Linux image's release, by SHA-256
#   xwin        a release, by SHA-256
#   CRT, SDK    explicit versions from Visual Studio 2022's channel manifest,
#               which xwin reads live; it checks every download against the
#               digest that manifest gives
#
# The base is passed in, as the exact tag scripts/build-linux.sh gives the
# Linux image, so this image is rebuilt whenever that one changes. It has no
# default on purpose (hence the skipped check at the top): there is no
# sensible base but that one.

ARG BASE_IMAGE
FROM ${BASE_IMAGE} AS tools

ARG DEBIAN_FRONTEND=noninteractive

# The rest of the base image's LLVM -- clang-cl (Ubuntu ships it in
# clang-tools), lld-link, and llvm-lib, llvm-rc and llvm-mt -- and Wine.
# 64-bit Wine only: no i386 multiarch, as nothing here is 32-bit.
RUN set -eu; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        "clang-tools-${LLVM_MAJOR}" "lld-${LLVM_MAJOR}" "llvm-${LLVM_MAJOR}" wine wine64; \
    rm -rf /var/lib/apt/lists/*; \
    for tool in clang-cl lld-link llvm-lib llvm-rc llvm-mt wine; do command -v "$tool"; done; \
    wine --version

# Rust's standard library for the target. Building a staticlib needs nothing
# more; linking a Rust executable (`cargo test`) needs the sdk stage below.
RUN rustup target add x86_64-pc-windows-msvc

# pluginval.exe, run under Wine. Checked to start, in a throw-away prefix.
ARG PLUGINVAL_VERSION=v1.0.4
ARG PLUGINVAL_SHA256_WINDOWS=c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab
RUN set -eu; \
    curl --proto '=https' --tlsv1.2 -sSfL -o /tmp/pluginval.zip \
      "https://github.com/Tracktion/pluginval/releases/download/${PLUGINVAL_VERSION}/pluginval_Windows.zip"; \
    echo "${PLUGINVAL_SHA256_WINDOWS}  /tmp/pluginval.zip" | sha256sum -c -; \
    unzip -q /tmp/pluginval.zip -d /opt/pluginval; \
    rm /tmp/pluginval.zip; \
    WINEPREFIX=/tmp/wineprefix WINEDEBUG=-all wine /opt/pluginval/pluginval.exe --version; \
    rm -rf /tmp/wineprefix

# Wine's fixme and err channels would bury pluginval's own output.
ENV WINEDEBUG=-all

LABEL org.opencontainers.image.title="NI cross-build: Windows tools" \
      org.opencontainers.image.description="clang-cl, lld-link, Rust for x86_64-pc-windows-msvc, Wine and pluginval.exe (Neon Ingvy)"

# ---------------------------------------------------------------------------
FROM tools AS sdk

# THE OWNER'S ACCEPTANCE OF MICROSOFT'S LICENCE, and nobody else's: the
# packages xwin downloads are Microsoft's Build Tools CRT and Windows SDK.
# README.md beside this file.
ARG XWIN_ACCEPT_LICENSE
ARG XWIN_VERSION=0.10.0
ARG XWIN_SHA256=d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88
ARG XWIN_MANIFEST_VERSION=17
ARG XWIN_CRT_VERSION=14.44.17.14
ARG XWIN_SDK_VERSION=10.0.26100
RUN set -eu; \
    if [ "${XWIN_ACCEPT_LICENSE:-}" != yes ]; then \
      echo "The MSVC C runtime and the Windows SDK are Microsoft's, under Microsoft's licence:" >&2; \
      echo "read tools/docker/windows/README.md. Building this stage needs XWIN_ACCEPT_LICENSE=yes." >&2; \
      exit 1; \
    fi; \
    curl --proto '=https' --tlsv1.2 -sSfL -o /tmp/xwin.tar.gz \
      "https://github.com/Jake-Shadle/xwin/releases/download/${XWIN_VERSION}/xwin-${XWIN_VERSION}-x86_64-unknown-linux-musl.tar.gz"; \
    echo "${XWIN_SHA256}  /tmp/xwin.tar.gz" | sha256sum -c -; \
    tar -xzf /tmp/xwin.tar.gz -C /tmp --strip-components=1 \
      "xwin-${XWIN_VERSION}-x86_64-unknown-linux-musl/xwin"; \
    /tmp/xwin --accept-license --manifest-version "$XWIN_MANIFEST_VERSION" \
      --crt-version "$XWIN_CRT_VERSION" --sdk-version "$XWIN_SDK_VERSION" \
      --cache-dir /tmp/xwin-cache splat --output /opt/xwin; \
    rm -rf /tmp/xwin /tmp/xwin.tar.gz /tmp/xwin-cache; \
    du -sh /opt/xwin

# What tools/cross/windows-clang-cl.cmake reads: the splat, and the compiler
# version its 14.44 CRT belongs to.
ENV NI_XWIN_DIR=/opt/xwin NI_MSVC_VERSION=19.44

# Cargo's configuration for the target, here rather than in a committed
# .cargo/config.toml: it is this image's, and means nothing outside it. A
# staticlib needs none of it (nothing is linked); `cargo test` links with
# lld-link against the splat and runs the test under Wine.
ENV CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER=lld-link \
    CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_RUSTFLAGS="-Lnative=/opt/xwin/crt/lib/x86_64 -Lnative=/opt/xwin/sdk/lib/um/x86_64 -Lnative=/opt/xwin/sdk/lib/ucrt/x86_64" \
    CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_RUNNER=wine

LABEL org.opencontainers.image.title="NI cross-build: Windows (local only)" \
      org.opencontainers.image.description="Contains Microsoft's CRT and Windows SDK under Microsoft's licence: build locally, never push"

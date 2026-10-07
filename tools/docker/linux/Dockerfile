# syntax=docker/dockerfile:1.27@sha256:4edf897a3ffa55b89f906fc8cc78afdb3f1834cc9c7083565e611a8a7d5fe99e
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# THE LINUX BUILD IMAGE: JUCE 9's Linux build dependencies, the pinned Rust
# toolchain, and pluginval under Xvfb. scripts/build-linux.sh builds it and runs
# every Linux build in it; tools/docker/windows/Dockerfile builds on top of it.
# docs/tech/cross-build.md is the walk-through.
#
# linux/amd64 is the platform that is built AND validated. linux/arm64 builds
# from the same file with the same pins, but pluginval publishes no arm64 Linux
# binary, so an arm64 image has no validator.
#
# EVERYTHING IN IT IS PINNED, so that rebuilding the image months from now
# compiles with the same tools:
#
#   frontend    the Dockerfile syntax above, by digest: the bare
#               docker/dockerfile:1 is whichever 1.x Docker Hub serves today
#   the base    ubuntu:24.04, by its multi-platform index digest
#   packages    ONE Ubuntu archive snapshot (snapshot.ubuntu.com), every one
#               of them. apt reads the archive as it was at UBUNTU_SNAPSHOT,
#               so clang, CMake and each -dev package keep the version they
#               had then. Pinning versions package by package breaks the day
#               Ubuntu supersedes one; a snapshot does not move.
#   Rust        rustup-init by version and SHA-256, the toolchain by version:
#               the pins of modules/_shared/Dockerfile, which move together
#               with these
#   pluginval   a release by SHA-256: the release scripts/validate-plugins.sh
#               pins for macOS
#
# To move the snapshot, set any later UTC timestamp (YYYYMMDDTHHMMSSZ) and
# rebuild; the build log names the version of every package it installed.

ARG UBUNTU=ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55

# THE ONE THING TAKEN FROM THE LIVE ARCHIVE, AND IT NEVER REACHES THE IMAGE.
# The snapshot service is HTTPS-only, and apt needs root certificates to reach
# it; the base image has none. This throw-away stage installs them from the
# live archive -- whatever ca-certificates, openssl and libssl it serves that
# day -- only so the step below can trust the snapshot's server while it
# fetches the snapshot's own. Nothing of it is copied: the bundle is mounted
# for that one step. TLS is not what makes the packages genuine either way:
# apt checks every index against Ubuntu's archive key.
FROM ${UBUNTU} AS bootstrap-ca
ARG DEBIAN_FRONTEND=noninteractive
RUN set -eu; \
    apt-get update; \
    apt-get install -y --no-install-recommends ca-certificates; \
    rm -rf /var/lib/apt/lists/*

FROM ${UBUNTU}

ARG DEBIAN_FRONTEND=noninteractive
ARG UBUNTU_SNAPSHOT=20261001T000000Z

# Every source -- the release, its updates and its security pocket -- points
# at the snapshot itself. One URL serves both architectures: the snapshot
# carries arm64 as well, which the live archive leaves to ports.ubuntu.com.
# Then the snapshot's ca-certificates, fetched with the bootstrap bundle and
# trusted from here on.
RUN --mount=type=bind,from=bootstrap-ca,source=/etc/ssl/certs/ca-certificates.crt,target=/tmp/bootstrap-ca.crt \
    set -eu; \
    sed -i -E "s#^URIs: .*#URIs: https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT}/#" \
        /etc/apt/sources.list.d/ubuntu.sources; \
    grep -E '^(URIs|Suites):' /etc/apt/sources.list.d/ubuntu.sources; \
    apt-get -o Acquire::https::CaInfo=/tmp/bootstrap-ca.crt update; \
    apt-get -o Acquire::https::CaInfo=/tmp/bootstrap-ca.crt install -y --no-install-recommends ca-certificates; \
    rm -rf /var/lib/apt/lists/*

# ONE LLVM FOR BOTH CONTAINERS: clang compiles the Linux build here, and the
# Windows image adds clang-cl and lld-link of the same release. Its bin
# directory goes first on PATH, so every tool is reachable by its plain name.
ENV LLVM_MAJOR=20
ENV PATH=/usr/lib/llvm-${LLVM_MAJOR}/bin:$PATH
ENV CC=clang CXX=clang++

# JUCE 9.0.3's Linux dependencies (its docs/Linux Dependencies.md), for what a
# plugin compiles with JUCE_WEB_BROWSER=0 and JUCE_USE_CURL=0 -- so no WebKit
# and no curl -- and nothing JUCE only needs with its defaults overridden:
#
#   juce_graphics      freetype, fontconfig (found through pkg-config)
#   juce_gui_basics    X11, Xcursor, Xext (XShm), Xinerama, Xrandr, and Xi:
#                      JUCE_USE_XINPUT is on by default since JUCE 9. Xrender
#                      and Xcomposite are NOT here -- JUCE_USE_XRENDER is off
#                      by default, and it is what includes both
#   juce_audio_devices ALSA, for a plugin that links juce_audio_utils
#
# No GL: only juce_opengl needs it (libglu1-mesa-dev mesa-common-dev
# libegl-dev, per the same document), and no plugin here uses it. No JACK and
# no LADSPA: both are off by default.
#
# build-essential is there even though clang compiles: it brings libstdc++, and
# JUCE builds its juceaide helper with the system compiler. Xvfb, xauth (which
# xvfb-run needs) and one font are for pluginval's editor tests.
RUN set -eu; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        build-essential "clang-${LLVM_MAJOR}" cmake ninja-build pkgconf \
        curl unzip \
        libasound2-dev libfreetype-dev libfontconfig1-dev \
        libx11-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxi-dev \
        xvfb xauth fonts-dejavu-core; \
    rm -rf /var/lib/apt/lists/*; \
    clang --version; cmake --version; ninja --version

# THE PINNED RUST TOOLCHAIN, by a pinned and verified installer -- the same
# block, and the same pins, as modules/_shared/Dockerfile (which says how to
# move them). Read-only at run time: the build scripts point CARGO_HOME into
# the build directory, so nothing a build writes lands in the image.
ARG RUSTUP_VERSION=1.29.1
ARG RUSTUP_SHA256_X86_64=dda7234360b7f578ca8b0ddcb80145646fa61a67c1720a5abc7051b35c9fcb71
ARG RUSTUP_SHA256_AARCH64=15f6e4ce9f583b929c996c91562bad6d4454f3281de858b02cdfdef615fac433
ARG RUST_TOOLCHAIN=1.98.1
ENV RUSTUP_HOME=/opt/rustup CARGO_HOME=/opt/cargo
ENV PATH=/opt/cargo/bin:$PATH
RUN set -eu; \
    case "$(uname -m)" in \
      x86_64)  triple=x86_64-unknown-linux-gnu;  sha="$RUSTUP_SHA256_X86_64" ;; \
      aarch64) triple=aarch64-unknown-linux-gnu; sha="$RUSTUP_SHA256_AARCH64" ;; \
      *) echo "no pinned rustup-init for $(uname -m)" >&2; exit 1 ;; \
    esac; \
    curl --proto '=https' --tlsv1.2 -sSfL -o /tmp/rustup-init \
      "https://static.rust-lang.org/rustup/archive/${RUSTUP_VERSION}/${triple}/rustup-init"; \
    echo "${sha}  /tmp/rustup-init" | sha256sum -c -; \
    chmod +x /tmp/rustup-init; \
    /tmp/rustup-init -y --no-modify-path --profile minimal --default-toolchain "$RUST_TOOLCHAIN"; \
    rm /tmp/rustup-init; \
    cargo --version

# PLUGINVAL, at strictness 10 under xvfb-run. Tracktion publish an x86_64
# Linux build only; an arm64 image says so here and scripts/build-linux.sh
# reports such a build as not validated rather than as passed.
ARG PLUGINVAL_VERSION=v1.0.4
ARG PLUGINVAL_SHA256_LINUX=c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352
RUN set -eu; \
    if [ "$(dpkg --print-architecture)" = amd64 ]; then \
      curl --proto '=https' --tlsv1.2 -sSfL -o /tmp/pluginval.zip \
        "https://github.com/Tracktion/pluginval/releases/download/${PLUGINVAL_VERSION}/pluginval_Linux.zip"; \
      echo "${PLUGINVAL_SHA256_LINUX}  /tmp/pluginval.zip" | sha256sum -c -; \
      mkdir -p /opt/pluginval; \
      unzip -q /tmp/pluginval.zip -d /opt/pluginval; \
      rm /tmp/pluginval.zip; \
      chmod 755 /opt/pluginval/pluginval; \
      /opt/pluginval/pluginval --version; \
    else \
      echo "pluginval ${PLUGINVAL_VERSION}: no Linux build for $(dpkg --print-architecture), so none in this image"; \
    fi

LABEL org.opencontainers.image.title="NI cross-build: Linux" \
      org.opencontainers.image.description="JUCE 9 + Rust plugin builds and pluginval for Linux (Neon Ingvy)"

WORKDIR /work

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber

FROM ubuntu:22.04

# ONE IMAGE FOR EVERY SCHWUNG MODULE. The modules differ in which crate they
# build and what they are called (modules/<name>/module.env), never in the
# toolchain -- two copies of this file were two chances for the golden render
# to be produced by two different compilers.

# The C toolchain is kept even though the modules are Rust: the linker for the
# aarch64 target is still gcc's, and cargo drives it rather than replacing it.
RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    curl ca-certificates make \
    && rm -rf /var/lib/apt/lists/*

# A PINNED TOOLCHAIN, INSTALLED BY A PINNED AND VERIFIED INSTALLER.
#
# This used to be `curl https://sh.rustup.rs | sh`: whatever that URL served on
# the day the image was built ran as root with no check at all. rustup-init is
# now fetched from rustup's versioned archive and refused unless it matches the
# digest written here. Both architectures, because Docker on Apple Silicon runs
# this image as arm64 and a CI runner runs it as x86_64.
#
# To move it: take RUSTUP_VERSION from
# https://static.rust-lang.org/rustup/release-stable.toml and each digest from
# .../rustup/archive/<version>/<triple>/rustup-init.sha256.
ARG RUSTUP_VERSION=1.29.1
ARG RUSTUP_SHA256_X86_64=dda7234360b7f578ca8b0ddcb80145646fa61a67c1720a5abc7051b35c9fcb71
ARG RUSTUP_SHA256_AARCH64=15f6e4ce9f583b929c996c91562bad6d4454f3281de858b02cdfdef615fac433
ARG RUST_TOOLCHAIN=1.98.1

# package.sh runs as the invoking user (docker run -u), not root, and cargo
# writes its lock and cache under CARGO_HOME -- so the toolchain is made
# GROUP-writable for group 0, which package.sh adds that user to. It used to be
# `chmod -R a+rwX`, world-writable, for the same reason.
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
    /tmp/rustup-init -y --no-modify-path --profile minimal \
      --default-toolchain "$RUST_TOOLCHAIN" \
      --target aarch64-unknown-linux-gnu; \
    rm /tmp/rustup-init; \
    chmod -R g+rwX /opt/cargo /opt/rustup

# cargo must be told which linker the target uses; without it the host's cc is
# invoked on aarch64 objects and the error names neither.
ENV CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc
ENV CROSS_PREFIX=aarch64-linux-gnu-

WORKDIR /build

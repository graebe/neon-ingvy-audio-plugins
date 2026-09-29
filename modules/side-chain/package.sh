#!/usr/bin/env bash
# Build and package the NI Side-Chain module for Schwung (Move, ARM64).
#
# THE SECOND OF TWO TARGETS AROUND ONE CORE. The plugin links `sc-capi` (see
# cmake/SideChainEngine.cmake); this builds `sc-move`, the Schwung audio_fx v2
# vtable. Both are crates in the repository's single Cargo workspace and both
# reach `sc-core` by relative path, so what ships here and what ships in the
# VST are the same DSP compiled twice, never two implementations.
#
# NOTE THE TWO NAMES. The directory is modules/side-chain/ (this product is `pump`
# everywhere in the repository) and the MODULE ID is `ni-side-chain`, because
# charlesvestal/schwung-ducker already owns `ducker` on the device.
#
#   ./modules/side-chain/package.sh     from anywhere
#   cmake --build build --target schwung same thing, through the build system
#
# Docker does the cross-compile unless CROSS_PREFIX is already set (which is
# how it runs INSIDE the container, and how CI runs it).
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# cargo, wherever it is installed. See scripts/rust-env.sh -- this used to be a
# bare `cargo` call and failed on any setup that did not put a shim in
# ~/.cargo/bin, which includes every Homebrew install.
. "$REPO_ROOT/scripts/rust-env.sh"
IMAGE_NAME="schwung-module-builder"
MODULE_ID="ni-side-chain"

if [ -z "$CROSS_PREFIX" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== NI Side-Chain module (via Docker) ==="
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "Building Docker image (first time only)..."
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
    fi
    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        "$IMAGE_NAME" \
        ./modules/side-chain/package.sh
    echo "=== Done ==="
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
cd "$REPO_ROOT"

echo "=== Building NI Side-Chain module ==="
echo "Cross prefix: $CROSS_PREFIX"

mkdir -p build "dist/$MODULE_ID"

echo "Compiling the Schwung wrapper (Rust)..."
# CARGO RUNS AT THE REPOSITORY ROOT, which is where the one workspace and its
# .cargo/config.toml live -- that config is what sets target-cpu=cortex-a72 for
# this triple, and cargo only finds it by walking UP from the working directory.
#
# -Ofast IS GONE AND CANNOT COME BACK. The C build used it, which let clang
# contract `a - b*c` into a fused multiply-add -- so the shipped .so and the
# suite that tested it were never bit-identical to each other. Rust does not
# contract, so the module and its tests now compute the same numbers, and the
# golden render pins the algorithm rather than a compiler flag.
cargo build --release -p sc-move --target aarch64-unknown-linux-gnu

# THE .so NAME IS LOAD-BEARING. For component_type audio_fx the chain host
# builds the path itself as modules/audio_fx/<id>/<id>.so and never reads
# module.json's "dsp" field. Name it dsp.so and the module simply does not
# load, with no error on screen -- one line in debug.log and nothing else.
cp "target/aarch64-unknown-linux-gnu/release/libsc_move.so" "build/${MODULE_ID}.so"
${CROSS_PREFIX}strip --strip-unneeded "build/${MODULE_ID}.so" 2>/dev/null || true
echo "  size: $(wc -c < "build/${MODULE_ID}.so") bytes"

echo "Packaging..."
cat "$SCRIPT_DIR/module.json"          > "dist/$MODULE_ID/module.json"
cat "build/${MODULE_ID}.so"            > "dist/$MODULE_ID/${MODULE_ID}.so"
# `if` AND NOT `[ -f x ] && cat`, WHICH WOULD END THE SCRIPT HERE.
#
# Under `set -e` an && list that fails at its FIRST command exits the shell, and
# NI Side-Chain deliberately ships no ui_chain.js -- the host draws its own knob grid
# from chain_params (see README.md). With the && form the packaging stopped at
# this line, before the LICENSE was copied, and the tarball came out without it:
# a licence-compliance failure caused by a shell idiom.
if [ -f "$SCRIPT_DIR/ui_chain.js" ]; then
    cat "$SCRIPT_DIR/ui_chain.js" > "dist/$MODULE_ID/ui_chain.js"
fi
if [ -f "$SCRIPT_DIR/help.json" ]; then
    cat "$SCRIPT_DIR/help.json" > "dist/$MODULE_ID/help.json"
fi
# THE LICENCE SHIPS WITH THE BINARY. What lands on a device is a .so and a .js
# with no repository anywhere near them, and MIT asks that the notice
# accompany the copy -- so it travels in the tarball or it does not reach the
# person it is addressed to at all. The repository's own LICENSE, since the
# engine's identical copy went when it became a subtree.
cat LICENSE                            > "dist/$MODULE_ID/LICENSE"
chmod +x "dist/$MODULE_ID/${MODULE_ID}.so"

cd dist
tar -czf "${MODULE_ID}-module.tar.gz" "$MODULE_ID/"
cd ..

echo ""
echo "=== Build Complete ==="
echo "Output:  dist/$MODULE_ID/"
echo "Tarball: dist/${MODULE_ID}-module.tar.gz"
ls -la "dist/$MODULE_ID/"

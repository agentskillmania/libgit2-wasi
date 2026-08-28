#!/bin/bash
# Build git-guest.wasm — a WASI Component that exports the subcommand interface.
#
# Output: build-component/git-guest.wasm
#
# Prerequisites:
#   - wasi-sdk 22+ at $HOME/wasi-sdk
#   - wit-bindgen 0.57+ in PATH
#   - libgit2 WASI build completed (run build_wasi.sh first)
#   - mbedTLS built for WASI at ../mbedtls/build-wasi/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WASI_SDK="${WASI_SDK:-$HOME/wasi-sdk}"
CC="$WASI_SDK/bin/wasm32-wasip2-clang"
AR="$WASI_SDK/bin/llvm-ar"
WIT_BINDGEN="${WIT_BINDGEN:-wit-bindgen}"
BUILD_DIR="$SCRIPT_DIR/build-component"
WIT_DIR="$SCRIPT_DIR/wit"
MBEDTLS_DIR="$SCRIPT_DIR/../mbedtls"
WASI_BUILD="$SCRIPT_DIR/build-wasi"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

# --- Prerequisite checks ---

if [ ! -f "$WASI_SDK/bin/wasm32-wasip2-clang" ]; then
    echo "Error: wasi-sdk not found at $WASI_SDK" >&2
    exit 1
fi

if ! command -v "$WIT_BINDGEN" &>/dev/null; then
    echo "Error: wit-bindgen not found in PATH" >&2
    exit 1
fi

# Ensure core library is built
if [ ! -f "$WASI_BUILD/libgit2.a" ]; then
    echo "=== Building libgit2.a first ==="
    bash "$SCRIPT_DIR/build_wasi.sh"
fi

MBEDTLS_LIB="$MBEDTLS_DIR/build-wasi/library"
for lib in libmbedtls.a libmbedx509.a libmbedcrypto.a; do
    if [ ! -f "$MBEDTLS_LIB/$lib" ]; then
        echo "Error: $MBEDTLS_LIB/$lib not found." >&2
        echo "Run mbedtls/build_wasi.sh first." >&2
        exit 1
    fi
done

echo "=== Building git-guest component ==="
echo "WASI_SDK:     $WASI_SDK"
echo "WIT:          $WIT_DIR"
echo "mbedTLS:      $MBEDTLS_DIR"
echo "Build dir:    $BUILD_DIR"
echo

# --- Prepare build directory ---

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR/bindings" "$BUILD_DIR/obj"

# --- Step 1: Generate guest bindings from WIT ---

echo "--- Generating guest bindings ---"
$WIT_BINDGEN c "$WIT_DIR" --world guest-git --out-dir "$BUILD_DIR/bindings"

# --- Step 2: Compiler flags ---

MBEDTLS_INC="$MBEDTLS_DIR/include"

CFLAGS="-O2"
CFLAGS+=" -DNO_MMAP -D__unix__ -D_WASI_EMULATED_SIGNAL"
CFLAGS+=" -DMBEDTLS_NO_PLATFORM_ENTROPY -Wno-incompatible-pointer-types"
CFLAGS+=" -Wno-implicit-function-declaration -Wno-int-conversion"

# Include paths (matching CMake CLI_INCLUDES + wasi_include + WIT bindings)
CFLAGS+=" -I$WASI_BUILD/src/util"
CFLAGS+=" -I$WASI_BUILD/include"
CFLAGS+=" -I$SCRIPT_DIR/src/util"
CFLAGS+=" -I$SCRIPT_DIR/src/cli"
CFLAGS+=" -I$SCRIPT_DIR/include"
CFLAGS+=" -I$SCRIPT_DIR/wasi_include"
CFLAGS+=" -I$MBEDTLS_INC"
CFLAGS+=" -I$BUILD_DIR/bindings"

# POSIX stub defines (matching build_wasi.sh)
CFLAGS+=" -Dgetuid()=getpid()"
CFLAGS+=" -Dgeteuid()=getpid()"
CFLAGS+=" -Dgetppid()=getpid()"
CFLAGS+=" -Dgetpgid(a)=getpid()"
CFLAGS+=" -Dgetsid(a)=getpid()"
CFLAGS+=" -Dgetgid()=getpid()"

# --- Step 3: Compile CLI sources ---

echo "--- Compiling CLI sources ---"
CLI_SRC_DIR="$SCRIPT_DIR/src/cli"

for src in "$CLI_SRC_DIR"/*.c "$CLI_SRC_DIR/unix"/*.c; do
    base="$(basename "${src%.c}")"
    obj="$BUILD_DIR/obj/cli_$base.o"
    echo "  CC  $(basename "$src")"
    $CC $CFLAGS -c "$src" -o "$obj"
done

# --- Step 4: Compile guest wrapper ---

echo "--- Compiling guest wrapper ---"
$CC $CFLAGS \
    -c "$CLI_SRC_DIR/component/guest_main.c" \
    -o "$BUILD_DIR/obj/guest_main.o"

# --- Step 5: Compile WIT bindings ---

echo "--- Compiling WIT bindings ---"
$CC $CFLAGS \
    -c "$BUILD_DIR/bindings/guest_git.c" \
    -o "$BUILD_DIR/obj/guest_git.o"

# --- Step 6: Link git-guest.wasm ---

echo "--- Linking git-guest.wasm ---"

# Collect all object files
CLI_OBJECTS=$(ls "$BUILD_DIR/obj"/cli_*.o)

$CC -O2 \
    -o "$BUILD_DIR/git-guest.wasm" \
    $CLI_OBJECTS \
    "$BUILD_DIR/obj/guest_main.o" \
    "$BUILD_DIR/obj/guest_git.o" \
    "$BUILD_DIR/bindings/guest_git_component_type.o" \
    "$WASI_BUILD/libgit2.a" \
    "$MBEDTLS_LIB/libmbedtls.a" \
    "$MBEDTLS_LIB/libmbedx509.a" \
    "$MBEDTLS_LIB/libmbedcrypto.a" \
    -lwasi-emulated-signal \
    -lwasi-emulated-process-clocks \
    -lwasi-emulated-getpid \
    -Wl,--initial-memory=67108864 \
    -Wl,-z,stack-size=2097152 \
    -Wl,--undefined=mbedtls_platform_entropy_poll \
    -Wl,--allow-undefined

echo
echo "=== Output ==="
ls -lh "$BUILD_DIR/git-guest.wasm"

# --- Step 7: Verify ---

echo
echo "=== Verify ==="
WASMTIME="${WASMTIME:-$HOME/bin/wasmtime}"
if [ -x "$WASMTIME" ]; then
    echo "--- WIT interface ---"
    wasm-tools component wit "$BUILD_DIR/git-guest.wasm" 2>/dev/null | grep -E "export|agentskillmania" || true
    echo
    echo "--- Test: git help ---"
    $WASMTIME -W exceptions=y "$BUILD_DIR/git-guest.wasm" help 2>&1 | head -10
else
    echo "(wasmtime not found at $WASMTIME, skipping verification)"
fi

echo
echo "Build succeeded."

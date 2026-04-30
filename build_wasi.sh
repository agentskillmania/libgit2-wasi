#!/bin/bash
# Build libgit2 v1.9.2 with mbedTLS HTTPS support for WASI preview2 (wasm32-wasip2)
#
# Output: build-wasi/git2.wasm
#
# Prerequisites:
#   - wasi-sdk 22+ at $HOME/wasi-sdk (override with WASI_SDK env var)
#   - mbedTLS built for WASI at ../mbedtls/build-wasi/ (run mbedtls/build_wasi.sh first)
#   - cmake, make

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WASI_SDK="${WASI_SDK:-$HOME/wasi-sdk}"
BUILD_DIR="$SCRIPT_DIR/build-wasi"
MBEDTLS_DIR="$SCRIPT_DIR/../mbedtls"

if [ ! -f "$WASI_SDK/bin/wasm32-wasip2-clang" ]; then
    echo "Error: wasi-sdk not found at $WASI_SDK" >&2
    echo "Set WASI_SDK env var to the correct path." >&2
    exit 1
fi

MBEDTLS_INC="$MBEDTLS_DIR/include"
MBEDTLS_LIB="$MBEDTLS_DIR/build-wasi/library"

for lib in libmbedtls.a libmbedx509.a libmbedcrypto.a; do
    if [ ! -f "$MBEDTLS_LIB/$lib" ]; then
        echo "Error: $MBEDTLS_LIB/$lib not found." >&2
        echo "Run mbedtls/build_wasi.sh first." >&2
        exit 1
    fi
done

echo "=== Building libgit2 + mbedTLS for WASI preview2 ==="
echo "WASI_SDK:    $WASI_SDK"
echo "mbedTLS:     $MBEDTLS_DIR"
echo "Build dir:   $BUILD_DIR"
echo

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. \
    -DCMAKE_C_COMPILER="$WASI_SDK/bin/wasm32-wasip2-clang" \
    -DCMAKE_C_COMPILER_TARGET=wasm32-wasip2 \
    -DCMAKE_SYSTEM_NAME=Generic \
    -DCMAKE_C_FLAGS="-DNO_MMAP -D__unix__ -D_WASI_EMULATED_SIGNAL -DMBEDTLS_NO_PLATFORM_ENTROPY -Wno-incompatible-pointer-types -I${SCRIPT_DIR}/wasi_include -Dgetuid\(\)=getpid\(\) -Dgeteuid\(\)=getpid\(\) -Dgetppid\(\)=getpid\(\) -Dgetpgid\(a\)=getpid\(\) -Dgetsid\(a\)=getpid\(\) -Dgetgid\(\)=getpid\(\)" \
    -DCMAKE_EXE_LINKER_FLAGS="-lwasi-emulated-signal -lwasi-emulated-process-clocks -lwasi-emulated-getpid -Wl,--initial-memory=67108864" \
    -DUSE_HTTPS=mbedTLS \
    -DMBEDTLS_INCLUDE_DIR="$MBEDTLS_INC" \
    -DMBEDTLS_LIBRARY="$MBEDTLS_LIB/libmbedtls.a" \
    -DMBEDX509_LIBRARY="$MBEDTLS_LIB/libmbedx509.a" \
    -DMBEDCRYPTO_LIBRARY="$MBEDTLS_LIB/libmbedcrypto.a"

# Build everything (wasi_entropy.c is handled by CMake automatically)
make -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" libgit2package git2_cli 2>&1 || true

if [ -f git2 ]; then
    mv git2 git2.wasm
fi

echo
echo "=== Output ==="
ls -lh "$BUILD_DIR/git2.wasm"
echo
echo "=== Verify ==="
WASMTIME="${WASMTIME:-$HOME/bin/wasmtime}"
if [ -x "$WASMTIME" ]; then
    "$WASMTIME" --version
    "$WASMTIME" -S inherit-network=yes "$BUILD_DIR/git2.wasm" help 2>&1 | head -10
else
    echo "(wasmtime not found at $WASMTIME, skipping verification)"
fi

echo
echo "Build succeeded."

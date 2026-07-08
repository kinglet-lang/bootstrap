#!/usr/bin/env bash
# Copy the LLVM shared library that kinglet needs into the dist directory and
# set rpath so the loader finds it alongside the binary without requiring a
# system-wide LLVM installation.
#
#   bash scripts/stage-llvm-libs.sh <dist-dir> <llvm-config>
#
# llvm-config is the path to the llvm-config binary used at build time.
set -euo pipefail

DIST="${1:?usage: stage-llvm-libs.sh <dist-dir> <llvm-config>}"
LLVM_CONFIG="${2:?usage: stage-llvm-libs.sh <dist-dir> <llvm-config>}"

LLVM_LIBDIR="$("$LLVM_CONFIG" --libdir)"

# Copy the LLVM shared library.
LLVM_SHARED="$LLVM_LIBDIR/libLLVM-$("$LLVM_CONFIG" --version).so"
if [ -f "$LLVM_SHARED" ]; then
  cp "$LLVM_SHARED" "$DIST/"
  echo "staged $(basename "$LLVM_SHARED") into $DIST"
else
  echo "stage-llvm-libs: $LLVM_SHARED not found; skipping" >&2
  exit 1
fi

# Set rpath to $ORIGIN so the loader finds the .so next to the binary.
BIN="$DIST/kinglet"
if [ -f "$BIN" ] && command -v patchelf >/dev/null 2>&1; then
  patchelf --set-rpath '$ORIGIN' "$BIN"
  echo "set rpath on $(basename "$BIN")"
else
  echo "stage-llvm-libs: patchelf not available, rpath not set" >&2
fi

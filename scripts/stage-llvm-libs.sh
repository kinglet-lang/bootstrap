#!/usr/bin/env bash
# Copy the LLVM shared library and its transitive runtime dependencies into
# the dist directory so kinglet runs without a system-wide LLVM installation.
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

# ---------- transitive dependency bundling ----------
# libLLVM pulls in libedit, libffi, libzstd, libtinfo, libxml2, and others
# depending on the CMake configuration.  Copy every NEEDED library that is
# not part of glibc / libstdc++ / libgcc so the binary is self-contained.

# System libraries we never bundle (the loader and libc are always present;
# libstdc++ / libgcc are ABI-stable enough that the system copy works).
SKIP_PATTERNS='/(ld-linux|libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so|libresolv\.so|libstdc\+\+\.so|libgcc_s\.so)'

copy_transitive_libs() {
  local lib="$1"
  local dest="$2"
  local visited_file="$3"

  # Already processed?
  grep -qxF "$lib" "$visited_file" 2>/dev/null && return 0
  echo "$lib" >> "$visited_file"

  ldd "$lib" 2>/dev/null | grep '=> /' | awk '{print $3}' | while IFS= read -r dep; do
    # Skip system libraries.
    echo "$dep" | grep -qE "$SKIP_PATTERNS" && continue

    local name
    name="$(basename "$dep")"
    if [ ! -f "$dest/$name" ]; then
      cp -a "$dep" "$dest/"
      echo "  staged $name (dep of $(basename "$lib"))"
      copy_transitive_libs "$dep" "$dest" "$visited_file"
    fi
  done
}

VISITED="$(mktemp)"
trap 'rm -f "$VISITED"' EXIT

echo "bundling transitive dependencies of libLLVM..."
copy_transitive_libs "$LLVM_SHARED" "$DIST" "$VISITED"

echo "llvm staging complete"

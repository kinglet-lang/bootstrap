#!/usr/bin/env bash
# Build the kinglet compiler and wire the resulting binary onto PATH.
#
#   bash scripts/build.sh                 # gn gen + ninja (release, LLVM if found)
#   bash scripts/build.sh --debug         # debug build (-g, no -O2)
#   bash scripts/build.sh --no-llvm       # force compile-only, no native backend
#   bash scripts/build.sh --out out/Foo   # custom output dir (default: out/Default)
#   bash scripts/build.sh --gn 'sanitizer="address,undefined"'  # append GN args
#
# Set BUILD_CI=1 to skip binary staging (CI/automation use).
#
# Requires scripts/setup.sh to have been run at least once (for GN + Ninja
# under ./tools/bin). This script re-sources setup.sh's helpers (LLVM
# detection, PATH wiring) without re-running the GN/Ninja/LLVM install step.
#
# After a successful build, `kinglet` (and `klet`, its alias) are staged and
# added to PATH via the same shell-profile mechanism as scripts/setup.sh.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLS="$ROOT/tools"
BIN="$TOOLS/bin"

OUT_DIR="out/Default"
IS_DEBUG=false
FORCE_NO_LLVM=false
GN_EXTRA=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug) IS_DEBUG=true; shift ;;
    --no-llvm) FORCE_NO_LLVM=true; shift ;;
    --out) OUT_DIR="${2:?--out requires a value}"; shift 2 ;;
    --out=*) OUT_DIR="${1#--out=}"; shift ;;
    --gn) GN_EXTRA="${2:?--gn requires a value}"; shift 2 ;;
    --gn=*) GN_EXTRA="${1#--gn=}"; shift ;;
    --help|-h)
      echo "usage: build.sh [--debug] [--no-llvm] [--out out/Dir] [--gn 'key=val ...']"
      exit 0
      ;;
    *)
      echo "build.sh: unknown option '$1'" >&2
      exit 2
      ;;
  esac
done

info()  { printf '\033[34m>\033[0m %s\n' "$*" >&2; }
warn()  { printf '\033[33m!\033[0m %s\n' "$*" >&2; }
err()   { printf '\033[31m✗\033[0m %s\n' "$*" >&2; }

cd "$ROOT"

# ========== prerequisites ==========

if [[ ! -x "$BIN/gn" || ! -x "$BIN/ninja" ]]; then
  err "GN/Ninja not found in $BIN"
  err "run 'bash scripts/setup.sh' first (one-time toolchain setup)"
  exit 1
fi
export PATH="$BIN:$PATH"

# Reuse setup.sh's LLVM detection + PATH-wiring helpers without re-running
# its install flow.
SETUP_SH_SKIP_MAIN=1 source "$SCRIPT_DIR/setup.sh"

# ========== configure ==========

GN_ARGS="is_debug=$IS_DEBUG"

if ! $FORCE_NO_LLVM; then
  if llvm_cfg="$(find_llvm_config)"; then
    info "found llvm-config: $llvm_cfg"
    GN_ARGS="$GN_ARGS enable_llvm=true llvm_config=\"$llvm_cfg\""
  else
    warn "no LLVM found — building without native backend (compile-only)"
    warn "install LLVM 18+ and re-run, or pass --no-llvm to silence this"
  fi
else
  info "--no-llvm: building without native backend"
fi

# Append extra GN args (e.g. sanitizer, coverage, custom flags).
if [[ -n "$GN_EXTRA" ]]; then
  GN_ARGS="$GN_ARGS $GN_EXTRA"
fi

# Resolve version from the nearest git tag (v0.1.0 -> 0.1.0).
# Respect an explicit kinglet_version= from --gn; if absent,
# auto-detect so local dev builds show the correct version.
if [[ "$GN_ARGS" != *kinglet_version=* ]]; then
  if ver="$(git -C "$ROOT" describe --tags --abbrev=0 2>/dev/null)"; then
    ver="${ver#v}"
    GN_ARGS="$GN_ARGS kinglet_version=\"$ver\""
  fi
fi

info "gn gen $OUT_DIR --args='$GN_ARGS'"
eval gn gen "$OUT_DIR" --args="'$GN_ARGS'"

# ========== build ==========

# kinglet_rt is LLVM-independent; always build it so compile-only runs still
# type-check the runtime (catches Windows/MinGW-only regressions like the
# missing <io.h> include that broke _close).
NINJA_TARGETS="kinglet kinglet_rt"

info "ninja -C $OUT_DIR $NINJA_TARGETS"
ninja -C "$OUT_DIR" $NINJA_TARGETS

BUILT_BIN="$ROOT/$OUT_DIR/kinglet"
if [[ ! -x "$BUILT_BIN" ]]; then
  err "build finished but $BUILT_BIN is missing"
  exit 1
fi

# ========== stage kinglet/klet + wire PATH ==========

if [[ "${BUILD_CI:-0}" == "1" ]]; then
  info "BUILD_CI=1: skipping binary staging"
  exit 0
fi

mkdir -p "$BIN"
cp -f "$BUILT_BIN" "$BIN/kinglet"
chmod +x "$BIN/kinglet"

# The kinglet binary resolves the runtime archive relative to its own
# directory (resolve_rt_lib in main.cc). Stage it alongside the binary.
if [[ "$GN_ARGS" == *enable_llvm=true* ]]; then
  RT_LIB="$ROOT/$OUT_DIR/obj/runtime/libkinglet_rt.a"
  if [[ -f "$RT_LIB" ]]; then
    cp -f "$RT_LIB" "$BIN/"
    info "staged $BIN/libkinglet_rt.a"
  fi
fi

if [[ -f "$SCRIPT_DIR/stage-klet-alias.sh" ]]; then
  bash "$SCRIPT_DIR/stage-klet-alias.sh" "$BIN" || warn "klet alias staging failed (non-fatal)"
fi

add_bin_to_path

info ""
info "Done: $BIN/kinglet"
"$BIN/kinglet" --version 2>/dev/null || true
info "Restart your shell, or it's already on PATH for this session."

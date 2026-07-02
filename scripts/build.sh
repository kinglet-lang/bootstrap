#!/usr/bin/env bash
# Build the kinglet compiler and wire the resulting binary onto PATH.
#
#   bash scripts/build.sh                 # gn gen + ninja (release, LLVM if found)
#   bash scripts/build.sh --debug         # debug build (-g, no -O2)
#   bash scripts/build.sh --no-llvm       # force compile-only, no native backend
#   bash scripts/build.sh --out out/Foo   # custom output dir (default: out/Default)
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

while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug) IS_DEBUG=true; shift ;;
    --no-llvm) FORCE_NO_LLVM=true; shift ;;
    --out) OUT_DIR="${2:?--out requires a value}"; shift 2 ;;
    --out=*) OUT_DIR="${1#--out=}"; shift ;;
    --help|-h)
      echo "usage: build.sh [--debug] [--no-llvm] [--out out/Dir]"
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

# ── prerequisites ────────────────────────────────────────────────────────────

if [[ ! -x "$BIN/gn" || ! -x "$BIN/ninja" ]]; then
  err "GN/Ninja not found in $BIN"
  err "run 'bash scripts/setup.sh' first (one-time toolchain setup)"
  exit 1
fi
export PATH="$BIN:$PATH"

# Reuse setup.sh's LLVM detection + PATH-wiring helpers without re-running
# its install flow.
SETUP_SH_SKIP_MAIN=1 source "$SCRIPT_DIR/setup.sh"

# ── configure ────────────────────────────────────────────────────────────────

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

info "gn gen $OUT_DIR --args='$GN_ARGS'"
eval gn gen "$OUT_DIR" --args="'$GN_ARGS'"

# ── build ────────────────────────────────────────────────────────────────────

NINJA_TARGETS="kinglet"
if [[ "$GN_ARGS" == *enable_llvm=true* ]]; then
  NINJA_TARGETS="kinglet kinglet_rt"
fi

info "ninja -C $OUT_DIR $NINJA_TARGETS"
ninja -C "$OUT_DIR" $NINJA_TARGETS

BUILT_BIN="$ROOT/$OUT_DIR/kinglet"
if [[ ! -x "$BUILT_BIN" ]]; then
  err "build finished but $BUILT_BIN is missing"
  exit 1
fi

# ── stage kinglet/klet + wire PATH ──────────────────────────────────────────

mkdir -p "$BIN"
cp -f "$BUILT_BIN" "$BIN/kinglet"
chmod +x "$BIN/kinglet"

if [[ -f "$SCRIPT_DIR/stage-klet-alias.sh" ]]; then
  bash "$SCRIPT_DIR/stage-klet-alias.sh" "$BIN" || warn "klet alias staging failed (non-fatal)"
fi

add_bin_to_path

info ""
info "Done: $BIN/kinglet"
"$BIN/kinglet" --version 2>/dev/null || true
info "Restart your shell, or it's already on PATH for this session."

#!/usr/bin/env bash
# ========== Kinglet fuzzing driver ==========
# Builds and runs the libFuzzer front-end fuzz targets, seeded from the small
# committed corpus under tests/fuzz/corpus/<target>. By default each run
# copies that seed set into a scratch corpus under the build directory and
# lets libFuzzer mutate/expand there — the scratch corpus is gitignored
# (lives under out/) and disposable, so a fuzzing run can never grow the
# tracked corpus on its own. Any crash, sanitizer report, OOM, or timeout is
# still written back next to the tracked seed corpus as crash-*/oom-*/
# timeout-* for triage, matching the "Adding a regression seed" workflow in
# tests/fuzz/README.md.
#
# Usage:
#   scripts/fuzz.sh [target] [seconds]
#
#   target    one of: lexer | parser | pipeline | all   (default: all)
#   seconds   wall-clock budget per target               (default: 60)
#
# Examples:
#   scripts/fuzz.sh                 # all targets, 60s each
#   scripts/fuzz.sh parser 300      # parser only, 5 minutes
#   scripts/fuzz.sh all 30          # quick smoke of every target
#
# Env:
#   KINGLET_FUZZ_IN_PLACE=1   Skip the scratch corpus and run libFuzzer
#                             directly against tests/fuzz/corpus/<target>, so
#                             mutated inputs accumulate in the tracked
#                             directory (the pre-scratch-corpus behavior).
#                             Only use this for a deliberate campaign where
#                             you intend to hand-pick and commit new seeds
#                             afterward — never as the default CI/local path.

set -euo pipefail

TARGET="${1:-all}"
SECONDS_PER="${2:-60}"
IN_PLACE="${KINGLET_FUZZ_IN_PLACE:-0}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

export PATH="$REPO_ROOT/tools/bin:$PATH"

BUILD_DIR="out/Fuzz"
SCRATCH_ROOT="$BUILD_DIR/corpus"

# ========== Configure ==========
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
  echo "==> gn gen $BUILD_DIR (use_libfuzzer, asan+ubsan)"
  gn gen "$BUILD_DIR" \
    --args='is_debug=false use_libfuzzer=true sanitizer="address,undefined"'
fi

# ========== Select targets ==========
case "$TARGET" in
  lexer)    TARGETS=(fuzz_lexer) ;;
  parser)   TARGETS=(fuzz_parser) ;;
  pipeline) TARGETS=(fuzz_pipeline) ;;
  all)      TARGETS=(fuzz_lexer fuzz_parser fuzz_pipeline) ;;
  *)
    echo "error: unknown target '$TARGET' (want: lexer|parser|pipeline|all)" >&2
    exit 2
    ;;
esac

# ========== Build ==========
echo "==> ninja -C $BUILD_DIR ${TARGETS[*]}"
ninja -C "$BUILD_DIR" "${TARGETS[@]}"

# ========== Run ==========
# Leaks are checked separately; halt immediately on the first real defect so
# the crashing input is written next to the tracked corpus for triage.
export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1:abort_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"

status=0
for t in "${TARGETS[@]}"; do
  name="${t#fuzz_}"
  tracked="tests/fuzz/corpus/$name"
  mkdir -p "$tracked"

  echo
  echo "========== $t (${SECONDS_PER}s) =========="

  if [ "$IN_PLACE" = "1" ]; then
    run_dir="$tracked"
    echo "!!! KINGLET_FUZZ_IN_PLACE=1: mutating $tracked directly" >&2
  else
    run_dir="$SCRATCH_ROOT/$name"
    mkdir -p "$run_dir"
    # Refresh the scratch corpus from the tracked seeds without disturbing
    # any already-expanded scratch state from a prior run in this build dir.
    find "$tracked" -maxdepth 1 -type f -exec cp -n {} "$run_dir/" \;
  fi

  if ! "./$BUILD_DIR/$t" "$run_dir" \
        -max_total_time="$SECONDS_PER" \
        -print_final_stats=1 \
        -artifact_prefix="$tracked/"; then
    echo "!!! $t found a defect (artifact written under $tracked/)" >&2
    status=1
  fi
done

if [ "$status" -eq 0 ]; then
  echo
  if [ "$IN_PLACE" = "1" ]; then
    echo "==> all fuzz targets survived their time budget with no defects"
  else
    echo "==> all fuzz targets survived their time budget with no defects"
    echo "==> scratch corpus under $SCRATCH_ROOT is disposable; tracked corpus under tests/fuzz/corpus is untouched"
  fi
fi
exit "$status"

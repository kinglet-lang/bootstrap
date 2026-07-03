#!/usr/bin/env bash
# ========== Kinglet fuzzing driver ==========
# Builds and runs the libFuzzer front-end fuzz targets against their seed
# corpora. Any crash, sanitizer report, OOM, or timeout is reported and the
# offending input is preserved under the corpus directory as crash-*.
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

set -euo pipefail

TARGET="${1:-all}"
SECONDS_PER="${2:-60}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

export PATH="$REPO_ROOT/tools/bin:$PATH"

BUILD_DIR="out/Fuzz"

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
# the crashing input is written next to the corpus for triage.
export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1:abort_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"

status=0
for t in "${TARGETS[@]}"; do
  corpus="tests/fuzz/corpus/${t#fuzz_}"
  mkdir -p "$corpus"
  echo
  echo "========== $t (${SECONDS_PER}s) =========="
  if ! "./$BUILD_DIR/$t" "$corpus" \
        -max_total_time="$SECONDS_PER" \
        -print_final_stats=1 \
        -artifact_prefix="$corpus/"; then
    echo "!!! $t found a defect (artifact written under $corpus/)" >&2
    status=1
  fi
done

if [ "$status" -eq 0 ]; then
  echo
  echo "==> all fuzz targets survived their time budget with no defects"
fi
exit "$status"

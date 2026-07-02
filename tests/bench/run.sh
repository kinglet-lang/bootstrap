#!/bin/bash
# Micro-benchmark runner. Compares wall time against baseline thresholds
# stored in baseline.txt. Exits non-zero if any benchmark exceeds its
# threshold by more than 10% (regression guard).
#
# Usage: tests/bench/run.sh <kinglet-binary>

set -euo pipefail

KINGLET="${1:?usage: run.sh <kinglet-binary>}"
BENCH_DIR="$(dirname "$0")"
BASELINE="$BENCH_DIR/baseline.txt"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

red()  { printf '\033[31m%s\033[0m\n' "$*"; }
green(){ printf '\033[32m%s\033[0m\n' "$*"; }

failed=0

run_bench() {
  local name="$1"
  shift
  local expect="$1"
  shift
  local code="$1"

  local src="$TMPDIR/${name}.kl"
  echo "$code" >"$src"

  local start elapsed
  start=$(date +%s%N)
  "$KINGLET" "$src" >/dev/null 2>&1
  elapsed=$(($(date +%s%N) - start))

  local sec
  sec=$(awk "BEGIN { printf \"%.3f\", $elapsed / 1e9 }")

  local limit
  limit=$(awk "BEGIN { printf \"%.3f\", $expect * 1.10 }")

  if awk "BEGIN { exit($sec > $limit ? 0 : 1) }"; then
    red "FAIL $name: ${sec}s > ${limit}s (budget ${expect}s + 10%)"
    return 1
  else
    green "OK   $name: ${sec}s (budget ${expect}s)"
    return 0
  fi
}

bench_loop_code='int main(){int n=100000000;int i=0;for(i=0;i<n;i=i+1){}return 0;}'
bench_concat_code='int main(){string s="";int i;for(i=0;i<100000;i=i+1){s=s+"x";}return 0;}'
bench_map_code='int main(){{string:int} m={};int i=0;for(i=0;i<1000000;i=i+1){string k="k"+(i%1000);m[k]=m[k]+1;}return 0;}'

while IFS= read -r line; do
  [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
  read -r name expect <<<"$line"
  case "$name" in
    bench_loop)
      run_bench "$name" "$expect" "$bench_loop_code" || failed=1
      ;;
    bench_concat)
      run_bench "$name" "$expect" "$bench_concat_code" || failed=1
      ;;
    bench_map)
      run_bench "$name" "$expect" "$bench_map_code" || failed=1
      ;;
  esac
done <"$BASELINE"

exit $failed

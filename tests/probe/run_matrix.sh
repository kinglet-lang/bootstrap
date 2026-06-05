#!/usr/bin/env bash
# Capability matrix probe runner (bootstrap compiler).
#
# For each probe in cases/*.kl, runs the compiler through four stages —
# parse / check / codegen / run — and records the FURTHEST stage it reaches.
# The run-stage oracle is the probe's line-1 `// EXPECT_OUT: <text>` comment.
#
# This is a capability SNAPSHOT, not a pass/fail gate: it always exits 0 and
# prints how far each probe gets, so the matrix can document known gaps as
# well as supported features. Behavioral pass/fail assertions live in the
# golden suite (tests/cli/run_golden.sh).
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KINGLET="${KINGLET:-$ROOT/out/Debug/kinglet}"
if [[ ! -x "$KINGLET" ]]; then
  echo "kinglet not found at $KINGLET (build with: ninja -C out/Debug)" >&2
  exit 2
fi

CASES="$ROOT/tests/probe/cases"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Furthest stage reached by the compiler.
# parse / check / codegen expect a clean exit; run compares stdout to EXPECT_OUT.
classify() {
  local f="$1" expect="$2"
  "$KINGLET" --ast      "$f" >/dev/null 2>"$TMP/e" || { echo "parse✗|$(head -1 "$TMP/e")"; return; }
  "$KINGLET" --check    "$f" >/dev/null 2>"$TMP/e" || { echo "chk✗|$(head -1 "$TMP/e")"; return; }
  "$KINGLET" --bytecode "$f" >/dev/null 2>"$TMP/e" || { echo "cg✗|$(head -1 "$TMP/e")"; return; }
  local out
  out=$("$KINGLET" "$f" 2>"$TMP/e"); local ec=$?
  if [[ $ec -ne 0 ]]; then echo "run✗(rt)|$(head -1 "$TMP/e")"; return; fi
  if [[ "$out" == "$expect" ]]; then echo "run✓|"; else echo "run≠out|got:'$out'"; fi
}

echo "# Kinglet capability matrix (bootstrap)"
echo
printf '| %-26s | %-10s | %-12s | %s |\n' "probe" "expect" "stage" "note"
printf '|%s|%s|%s|%s|\n' "----------------------------" "------------" "--------------" "------------"

reached_run=0
total=0
for f in "$CASES"/*.kl; do
  name=$(basename "$f" .kl)
  expect=$(head -1 "$f" | sed -n 's/.*EXPECT_OUT:[[:space:]]*\(.*\)/\1/p')
  [[ -z "$expect" ]] && expect="<no oracle>"
  total=$((total + 1))

  res=$(classify "$f" "$expect"); cell=${res%%|*}; note=${res#*|}
  [[ "$cell" == "run✓" ]] && reached_run=$((reached_run + 1))
  printf '| %-26s | %-10s | %-12s | %s |\n' "$name" "$expect" "$cell" "$note"
done

echo
echo "Total: $total probes,  run✓: $reached_run"

#!/usr/bin/env bash
# Regression guard for ADR 0024 (C1): parser recovery paths must not splice
# a garbage recovery token's text into an AST node that TypeChecker later
# looks up, producing a diagnostic that names that garbage text instead of
# describing the real syntax error. Runs sema_probe (which calls
# TypeChecker::check() directly on the partial AST, bypassing the CLI's
# early-exit-on-parse-error path) against fixtures under sema_probe_cases/
# and asserts none of each case's `// CHECK-NOT:` strings appear in the
# probe's combined output.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ -x "$ROOT/out/Debug/sema_probe" ]]; then
  PROBE="$ROOT/out/Debug/sema_probe"
elif [[ -x "$ROOT/out/Default/sema_probe" ]]; then
  PROBE="$ROOT/out/Default/sema_probe"
else
  echo "sema_probe not found (build with: ninja -C out/Default probes)" >&2
  exit 2
fi

CASES_DIR="$ROOT/tests/probe/sema_probe_cases"
FAILURES=0
TOTAL=0

for f in "$CASES_DIR"/*.kl; do
  name="$(basename "$f" .kl)"
  TOTAL=$((TOTAL + 1))
  output="$("$PROBE" "$f" 2>&1)"

  case_failed=0
  while IFS= read -r line; do
    [[ "$line" == "// CHECK-NOT:"* ]] || continue
    needle="${line#// CHECK-NOT:}"
    needle="${needle#"${needle%%[![:space:]]*}"}" # trim leading space
    if grep -qF "$needle" <<<"$output"; then
      echo "FAIL  $name: found forbidden string: $needle" >&2
      case_failed=1
    fi
  done <"$f"

  if [[ $case_failed -eq 0 ]]; then
    echo "PASS  $name"
  else
    FAILURES=$((FAILURES + 1))
    echo "$output" | sed 's/^/      /' >&2
  fi
done

echo "================================="
echo "Total: $TOTAL  Failed: $FAILURES"
[[ $FAILURES -eq 0 ]]

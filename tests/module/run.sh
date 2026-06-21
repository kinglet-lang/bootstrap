#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KINGLET="${KINGLET_BOOTSTRAP:-$ROOT/out/Default/kinglet}"
FIXTURE="$(cd "$ROOT/../test" 2>/dev/null && pwd || true)"

if [[ ! -x "$KINGLET" ]]; then
  echo "kinglet not found at $KINGLET" >&2
  exit 2
fi

if [[ -z "$FIXTURE" || ! -d "$FIXTURE" ]]; then
  echo "SKIP: module integration (../test fixture not present)"
  exit 0
fi

failures=0

expect_check_ok() {
  local file="$1"
  if ! "$KINGLET" --check "$file" >/dev/null 2>&1; then
    echo "FAIL: expected --check ok for $file" >&2
    failures=$((failures + 1))
  fi
}

expect_check_fail() {
  local file="$1"
  if "$KINGLET" --check "$file" >/dev/null 2>&1; then
    echo "FAIL: expected --check fail for $file" >&2
    failures=$((failures + 1))
  fi
}

CASES="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

expect_check_ok "$FIXTURE/apps/demo/main.kl"
expect_check_ok "$FIXTURE/apps/bench/main.kl"
expect_check_fail "$CASES/export_mismatch/main.kl"
expect_check_fail "$CASES/unknown_module/main.kl"

if [[ "$failures" -eq 0 ]]; then
  echo "All module integration tests passed."
  exit 0
fi
echo "$failures module test(s) failed." >&2
exit 1

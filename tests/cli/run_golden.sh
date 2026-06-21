#!/usr/bin/env bash
# Deprecated wrapper — use tests/run_all.sh or individual suite runners.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "note: tests/cli/run_golden.sh is deprecated; running ADR 0012 suites" >&2
FAIL=0
for suite in sema parser codegen ir exec; do
  if ! bash "$ROOT/tests/$suite/run.sh"; then
    FAIL=1
  fi
done
exit "$FAIL"

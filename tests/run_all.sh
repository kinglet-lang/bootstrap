#!/usr/bin/env bash
# Bootstrap test orchestrator (decision 0012, bootstrap profile).
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=tests/common.sh
source "$ROOT/tests/common.sh"

export_kinglet "$ROOT" || exit 2

echo "========================================"
echo "Bootstrap Test Suite"
echo "========================================"

TOTAL=0
PASSED=0

run_suite() {
  TOTAL=$((TOTAL + 1))
  echo "Running: $1"
  if bash "$2"; then
    echo "✓ $1 PASSED"
    PASSED=$((PASSED + 1))
  else
    echo "✗ $1 FAILED"
  fi
  echo
}

run_suite "Sema (pass + fail)" "$ROOT/tests/sema/run.sh"
run_suite "Parser (AST)" "$ROOT/tests/parser/run.sh"
run_suite "Codegen (bytecode)" "$ROOT/tests/codegen/run.sh"
run_suite "IR (KIR dump)" "$ROOT/tests/ir/run.sh"
run_suite "Exec (native run)" "$ROOT/tests/exec/run.sh"
run_suite "Module integration" "$ROOT/tests/module/run.sh"
run_suite "Module hierarchical" "$ROOT/tests/module/hierarchical/run.sh"
run_suite "Nest manifest" "$ROOT/tests/nest/run_golden.sh"
run_suite "Formatter" "$ROOT/tests/fmt/run_golden.sh"
run_suite "ABI cross-module struct" "$ROOT/tests/abi/cross_module_struct/run.sh"

bash "$ROOT/tests/probe/run_matrix.sh"
TOTAL=$((TOTAL + 1))
PASSED=$((PASSED + 1))

echo "Summary: $PASSED/$TOTAL suites passed"
[[ "$PASSED" -eq "$TOTAL" ]] && exit 0 || exit 1

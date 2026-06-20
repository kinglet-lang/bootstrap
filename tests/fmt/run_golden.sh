#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ -x "$ROOT/out/Debug/kinglet" ]]; then
  KINGLET="$ROOT/out/Debug/kinglet"
elif [[ -x "$ROOT/out/Default/kinglet" ]]; then
  KINGLET="$ROOT/out/Default/kinglet"
else
  KINGLET="$ROOT/out/Debug/kinglet"
fi

CASES_DIR="$ROOT/tests/fmt/cases"
TMP_DIR="$(mktemp -d)"
FAILURES=0

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

strip_cr() {
  local f
  for f in "$@"; do
    [[ -f "$f" ]] || continue
    tr -d '\r' <"$f" >"$f.nocr" && mv -f "$f.nocr" "$f"
  done
}

fail() {
  echo "FAIL: $1" >&2
  FAILURES=$((FAILURES + 1))
}

run_stdin_case() {
  local name="$1"
  local input="$CASES_DIR/$name.kl"
  local expected="$CASES_DIR/$name.expected"
  local actual="$TMP_DIR/$name.out"

  "$KINGLET" fmt --stdin <"$input" >"$actual"
  local rc=$?
  if [[ $rc -ne 0 ]]; then
    fail "$name: kinglet fmt --stdin exited $rc"
    return
  fi
  strip_cr "$actual"
  if ! diff -u "$expected" "$actual" >/dev/null; then
    echo "output mismatch for $name:" >&2
    diff -u "$expected" "$actual" >&2
    FAILURES=$((FAILURES + 1))
  fi
}

run_project_case() {
  local dir="$1"
  local name
  name="$(basename "$dir")"
  local actual="$TMP_DIR/$name.out"
  local config="$dir/kinglet.nest"

  "$KINGLET" fmt --config "$config" --stdin <"$dir/input.kl" >"$actual"
  local rc=$?
  if [[ $rc -ne 0 ]]; then
    fail "$name: kinglet fmt exited $rc"
    return
  fi
  strip_cr "$actual"
  if ! diff -u "$dir/input.expected" "$actual" >/dev/null; then
    echo "output mismatch for $name:" >&2
    diff -u "$dir/input.expected" "$actual" >&2
    FAILURES=$((FAILURES + 1))
  fi
}

run_stdin_case basic_spacing
run_stdin_case leading_comment
run_stdin_case control_flow
run_stdin_case pipe
run_stdin_case inline_comments
run_project_case "$CASES_DIR/group_using_project"
run_project_case "$CASES_DIR/align_imports"
run_project_case "$CASES_DIR/align_struct_fields"

# parse error should fail
if "$KINGLET" fmt --stdin <"$CASES_DIR/parse_error.kl" >/dev/null 2>"$TMP_DIR/parse_error.stderr"; then
  fail "parse_error: expected non-zero exit"
fi

# --check should report unformatted file
CHECK_INPUT="$TMP_DIR/check_input.kl"
CHECK_EXPECTED="$CASES_DIR/basic_spacing.expected"
cp "$CASES_DIR/basic_spacing.kl" "$CHECK_INPUT"
if "$KINGLET" fmt --check "$CHECK_INPUT" >/dev/null 2>&1; then
  fail "basic_spacing --check: expected exit 1"
fi
"$KINGLET" fmt --write "$CHECK_INPUT" >/dev/null
if ! "$KINGLET" fmt --check "$CHECK_INPUT" >/dev/null 2>&1; then
  fail "basic_spacing --check after write: expected exit 0"
fi
strip_cr "$CHECK_INPUT"
if ! diff -u "$CHECK_EXPECTED" "$CHECK_INPUT" >/dev/null; then
  fail "basic_spacing --write: file content mismatch"
fi

if [[ $FAILURES -eq 0 ]]; then
  echo "All fmt tests passed."
  exit 0
fi
echo "$FAILURES fmt test(s) failed." >&2
exit 1

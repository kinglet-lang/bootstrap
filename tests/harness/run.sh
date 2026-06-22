#!/usr/bin/env bash
# Unified test harness for the bootstrap compiler (decision 0012, bootstrap profile).
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/common.sh
source "$ROOT/tests/common.sh"

KINGLET_BIN=""
TMP=""
FAILURES=0
PASSED=0
SKIPPED=0

RUN=""
RUN_ARGS=""
COMPILE_FAIL=0
EXPECT_STDOUT=""
EXPECT_STDERR=""
EXPECT_EXIT=""
CHECK_LINES=""
CHECK_NOT_LINES=""
CHECK_ERR_LINES=""
CHECK_ERR_AT=""

cleanup() { [[ -n "$TMP" ]] && rm -rf "$TMP"; }
trap cleanup EXIT

reset_case() {
  RUN=""
  RUN_ARGS=""
  COMPILE_FAIL=0
  EXPECT_STDOUT=""
  EXPECT_STDERR=""
  EXPECT_EXIT=""
  CHECK_LINES=""
  CHECK_NOT_LINES=""
  CHECK_ERR_LINES=""
  CHECK_ERR_AT=""
}

trim() { echo "$1" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//'; }

is_directive_line() {
  case "$1" in
    //\ RUN:*|//\ RUN-ARGS:*|//\ EXPECT-*|//\ CHECK:*|//\ CHECK-NOT:*|//\ CHECK-ERR:*|//\ CHECK-ERR-AT:*|//\ COMPILE-FAIL*) return 0 ;;
    *) return 1 ;;
  esac
}

parse_directives() {
  local f="$1"
  reset_case
  local in_block=1
  local line
  while IFS= read -r line || [[ -n "$line" ]]; do
    if [[ "$in_block" -eq 1 && -z "$(trim "$line")" ]]; then
      continue
    fi
    if [[ "$in_block" -eq 1 ]] && is_directive_line "$line"; then
      case "$line" in
        //\ RUN:*)
          RUN=$(trim "${line#// RUN:}")
          ;;
        //\ RUN-ARGS:*)
          RUN_ARGS=$(trim "${line#// RUN-ARGS:}")
          ;;
        //\ EXPECT-STDOUT:*)
          EXPECT_STDOUT=$(trim "${line#// EXPECT-STDOUT:}")
          ;;
        //\ EXPECT-STDERR:*)
          EXPECT_STDERR=$(trim "${line#// EXPECT-STDERR:}")
          ;;
        //\ EXPECT-EXIT:*)
          EXPECT_EXIT=$(trim "${line#// EXPECT-EXIT:}")
          ;;
        //\ CHECK-NOT:*)
          CHECK_NOT_LINES="${CHECK_NOT_LINES}$(trim "${line#// CHECK-NOT:}")"$'\n'
          ;;
        //\ CHECK-ERR-AT:*)
          CHECK_ERR_AT=$(trim "${line#// CHECK-ERR-AT:}")
          ;;
        //\ CHECK-ERR:*)
          CHECK_ERR_LINES="${CHECK_ERR_LINES}$(trim "${line#// CHECK-ERR:}")"$'\n'
          ;;
        //\ CHECK:*)
          CHECK_LINES="${CHECK_LINES}$(trim "${line#// CHECK:}")"$'\n'
          ;;
        //\ COMPILE-FAIL)
          COMPILE_FAIL=1
          ;;
      esac
      continue
    fi
    in_block=0
    break
  done <"$f"

  local base="${f%.kl}"
  if [[ -z "$EXPECT_STDOUT" && -f "${base}.expected" ]]; then
    EXPECT_STDOUT=$(cat "${base}.expected")
  fi
  if [[ -z "$EXPECT_STDERR" && -f "${base}.stderr" ]]; then
    EXPECT_STDERR=$(cat "${base}.stderr")
  fi
  if [[ -z "$EXPECT_EXIT" && -f "${base}.exit" ]]; then
    EXPECT_EXIT=$(cat "${base}.exit")
  fi
  if [[ -z "$CHECK_ERR_LINES" && -f "${base}.stderr_contains" ]]; then
    CHECK_ERR_LINES=$(cat "${base}.stderr_contains")$'\n'
  fi
  if [[ -z "$RUN_ARGS" && -f "${base}.args" ]]; then
    RUN_ARGS=$(tr '\n' ' ' <"${base}.args" | sed 's/[[:space:]]*$//')
  fi
  if [[ -z "$EXPECT_EXIT" ]]; then
    if [[ "$COMPILE_FAIL" -eq 1 ]]; then
      EXPECT_EXIT=65
    else
      EXPECT_EXIT=0
    fi
  fi
}

fail_case() {
  local name="$1"
  shift
  echo "FAIL  $name: $*" >&2
  FAILURES=$((FAILURES + 1))
}

pass_case() {
  local name="$1"
  echo "PASS  $name"
  PASSED=$((PASSED + 1))
}

skip_case() {
  local name="$1"
  local reason="$2"
  echo "SKIP  $name ($reason)" >&2
  SKIPPED=$((SKIPPED + 1))
}

assert_output() {
  local name="$1"
  local label="$2"
  local expect="$3"
  local actual="$4"
  if [[ "$expect" != "$actual" ]]; then
    fail_case "$name" "$label mismatch"
    diff -u <(printf '%s' "$expect") <(printf '%s' "$actual") | sed 's/^/      /' >&2
    return 1
  fi
  return 0
}

assert_exit() {
  local name="$1"
  local want="$2"
  local got="$3"
  if [[ "$want" != "$got" ]]; then
    fail_case "$name" "exit want=$want got=$got"
    return 1
  fi
  return 0
}

assert_substr() {
  local name="$1"
  local haystack="$2"
  local needle="$3"
  local label="$4"
  if ! grep -qF "$needle" <<<"$haystack"; then
    fail_case "$name" "$label missing $(printf '%q' "$needle")"
    echo "$haystack" | sed 's/^/        /' >&2
    return 1
  fi
  return 0
}

assert_substr_not() {
  local name="$1"
  local haystack="$2"
  local needle="$3"
  if grep -qF "$needle" <<<"$haystack"; then
    fail_case "$name" "unexpected $(printf '%q' "$needle")"
    return 1
  fi
  return 0
}

assert_check_list() {
  local name="$1"
  local haystack="$2"
  local list="$3"
  local label="$4"
  local ok=0
  local item
  while IFS= read -r item; do
    [[ -z "$item" ]] && continue
    if ! assert_substr "$name" "$haystack" "$item" "$label"; then
      ok=1
    fi
  done <<<"$list"
  return $ok
}

expand_run_args() {
  local src="$1"
  local raw="$2"
  local case_dir
  case_dir="$(cd "$(dirname "$src")" && pwd)"
  local expanded="${raw//\$HARNESS_TMP/$TMP}"
  expanded="${expanded//\$CASE_DIR/$case_dir}"
  printf '%s' "$expanded"
}

expand_run_args_to_array() {
  local src="$1"
  RUN_ARGS_ITEMS=()
  local expanded
  expanded="$(expand_run_args "$src" "$RUN_ARGS")"
  [[ -z "$expanded" ]] && return 0
  local token
  for token in $expanded; do
    RUN_ARGS_ITEMS+=("$token")
  done
}

run_pipeline() {
  local src="$1"
  local stdout="$2"
  local stderr="$3"
  expand_run_args_to_array "$src"
  local ec=0
  if ((${#RUN_ARGS_ITEMS[@]} > 0)); then
    "$KINGLET_BIN" "$src" "${RUN_ARGS_ITEMS[@]}" >"$stdout" 2>"$stderr" || ec=$?
  else
    "$KINGLET_BIN" "$src" >"$stdout" 2>"$stderr" || ec=$?
  fi
  echo "$ec"
}

run_check_pipeline() {
  local src="$1"
  local stdout="$2"
  local stderr="$3"
  local ec=0
  "$KINGLET_BIN" --check "$src" >"$stdout" 2>"$stderr" || ec=$?
  echo "$ec"
}

run_bytecode_pipeline() {
  local src="$1"
  local stdout="$2"
  local stderr="$3"
  local ec=0
  "$KINGLET_BIN" --bytecode "$src" >"$stdout" 2>"$stderr" || ec=$?
  echo "$ec"
}

run_ir_pipeline() {
  local src="$1"
  local stdout="$2"
  local stderr="$3"
  local ec=0
  "$KINGLET_BIN" --ir "$src" >"$stdout" 2>"$stderr" || ec=$?
  echo "$ec"
}

run_ast_pipeline() {
  local src="$1"
  local stdout="$2"
  local stderr="$3"
  local ec=0
  "$KINGLET_BIN" --ast "$src" >"$stdout" 2>"$stderr" || ec=$?
  echo "$ec"
}

finalize_case() {
  local name="$1"
  local stdout="$2"
  local stderr="$3"
  local ec="$4"
  local failed=0

  strip_cr "$stdout" "$stderr"
  assert_exit "$name" "$EXPECT_EXIT" "$ec" || failed=1
  if [[ -n "$EXPECT_STDOUT" ]]; then
    assert_output "$name" "stdout" "$EXPECT_STDOUT" "$(cat "$stdout")" || failed=1
  fi
  if [[ -n "$EXPECT_STDERR" ]]; then
    assert_output "$name" "stderr" "$EXPECT_STDERR" "$(cat "$stderr")" || failed=1
  fi
  assert_check_list "$name" "$(cat "$stdout")" "$CHECK_LINES" "stdout" || failed=1
  while IFS= read -r item; do
    [[ -z "$item" ]] && continue
    assert_substr_not "$name" "$(cat "$stdout")" "$item" || failed=1
  done <<<"$CHECK_NOT_LINES"
  assert_check_list "$name" "$(cat "$stderr")" "$CHECK_ERR_LINES" "stderr" || failed=1
  if [[ -n "$CHECK_ERR_AT" ]]; then
    assert_substr "$name" "$(cat "$stderr")" "${CHECK_ERR_AT}:" "stderr at" || failed=1
  fi
  if [[ "$failed" -eq 1 && -s "$stderr" ]]; then
    cat "$stderr" | sed 's/^/      /' >&2
  fi
  [[ "$failed" -eq 0 ]] && pass_case "$name"
}

run_one_file() {
  local src="$1"
  local name="${src#$ROOT/}"
  parse_directives "$src"

  if [[ -z "$RUN" ]]; then
    skip_case "$name" "no RUN directive"
    return
  fi

  if [[ "$RUN" == "run" && "${KINGLET_SKIP_RUN:-}" == "1" ]]; then
    skip_case "$name" "KINGLET_SKIP_RUN"
    return
  fi

  local stdout="$TMP/out.stdout"
  local stderr="$TMP/out.stderr"
  local ec=0

  case "$RUN" in
    run)
      ec=$(run_pipeline "$src" "$stdout" "$stderr")
      finalize_case "$name" "$stdout" "$stderr" "$ec"
      ;;

    check)
      ec=$(run_check_pipeline "$src" "$stdout" "$stderr")
      strip_cr "$stdout" "$stderr"
      if [[ "$COMPILE_FAIL" -eq 1 ]]; then
        local failed=0
        if [[ "$ec" -eq 0 ]]; then
          fail_case "$name" "expected check failure, got exit 0"
          return
        fi
        assert_exit "$name" "$EXPECT_EXIT" "$ec" || failed=1
        assert_check_list "$name" "$(cat "$stderr")" "$CHECK_ERR_LINES" "stderr" || failed=1
        if [[ -n "$CHECK_ERR_AT" ]]; then
          assert_substr "$name" "$(cat "$stderr")" "${CHECK_ERR_AT}:" "stderr at" || failed=1
        fi
        [[ "$failed" -eq 0 ]] && pass_case "$name"
        return
      fi
      finalize_case "$name" "$stdout" "$stderr" "$ec"
      ;;

    bytecode)
      ec=$(run_bytecode_pipeline "$src" "$stdout" "$stderr")
      strip_cr "$stdout" "$stderr"
      local golden="${src%.kl}.bytecode"
      if [[ -f "$golden" ]]; then
        if ! diff -u "$golden" "$stdout" >/dev/null; then
          fail_case "$name" "bytecode golden mismatch"
          diff -u "$golden" "$stdout" | sed 's/^/      /' | head -20 >&2
          return
        fi
        pass_case "$name"
        return
      fi
      if [[ -n "$CHECK_LINES" ]]; then
        local failed=0
        assert_exit "$name" "$EXPECT_EXIT" "$ec" || failed=1
        [[ -s "$stderr" ]] && fail_case "$name" "unexpected stderr" && failed=1
        assert_check_list "$name" "$(cat "$stdout")" "$CHECK_LINES" "stdout" || failed=1
        [[ "$failed" -eq 0 ]] && pass_case "$name"
        return
      fi
      skip_case "$name" "bytecode: no .bytecode golden or CHECK lines"
      ;;

    ir|ast)
      if [[ "$RUN" == "ir" ]]; then
        ec=$(run_ir_pipeline "$src" "$stdout" "$stderr")
      else
        ec=$(run_ast_pipeline "$src" "$stdout" "$stderr")
      fi
      strip_cr "$stdout" "$stderr"
      if [[ "$ec" -ne 0 ]]; then
        fail_case "$name" "$RUN dump failed (exit $ec)"
        cat "$stderr" | sed 's/^/      /' >&2
        return
      fi
      if [[ -s "$stderr" ]]; then
        fail_case "$name" "unexpected stderr"
        cat "$stderr" | sed 's/^/      /' >&2
        return
      fi
      if [[ -n "$CHECK_LINES" ]]; then
        local failed=0
        assert_check_list "$name" "$(cat "$stdout")" "$CHECK_LINES" "stdout" || failed=1
        [[ "$failed" -eq 0 ]] && pass_case "$name"
        return
      fi
      skip_case "$name" "$RUN: no CHECK lines"
      ;;

    *)
      skip_case "$name" "unknown RUN pipeline '$RUN'"
      ;;
  esac
}

collect_files() {
  local path="$1"
  if [[ -f "$path" && "$path" == *.kl ]]; then
    echo "$path"
    return
  fi
  if [[ -d "$path" ]]; then
    find "$path" -name '*.kl' -type f | sort
  fi
}

main() {
  if [[ "$#" -eq 0 ]]; then
    echo "usage: $0 <dir-or-file> ..." >&2
    echo "See tests/harness/directives.md" >&2
    exit 2
  fi

  export_kinglet "$ROOT" || exit 2
  KINGLET_BIN="$KINGLET"
  maybe_rebuild_kinglet "$ROOT"
  TMP="$(mktemp -d)"

  echo "Harness (bootstrap $KINGLET_BIN)"
  echo "================================="

  local path f
  for path in "$@"; do
    while IFS= read -r f; do
      [[ -z "$f" ]] && continue
      run_one_file "$f"
    done < <(collect_files "$path")
  done

  echo "================================="
  echo "Passed: $PASSED  Failed: $FAILURES  Skipped: $SKIPPED"
  [[ "$FAILURES" -eq 0 ]]
}

main "$@"

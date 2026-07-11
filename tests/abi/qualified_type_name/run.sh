#!/usr/bin/env bash
# ADR 0025: namespace-qualified type names for imported struct types.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CASE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KINGLET="${KINGLET:-${KINGLET_BOOTSTRAP:-}}"
if [[ -z "$KINGLET" ]]; then
  for candidate in "$ROOT/out/Llvm/kinglet" "$ROOT/out/Debug/kinglet" "$ROOT/out/Default/kinglet"; do
    if [[ -x "$candidate" ]]; then
      KINGLET="$candidate"
      break
    fi
  done
fi
if [[ ! -x "$KINGLET" ]]; then
  echo "kinglet not found (set KINGLET or build out/Llvm/kinglet)" >&2
  exit 2
fi

# Direct canonical qualified type name (abi::qualified_type_name::pair::Pair).
"$KINGLET" --check "$CASE/main.kl" >/dev/null
set +e
"$KINGLET" "$CASE/main.kl" >/dev/null
ec=$?
set -e
if [[ "$ec" -ne 37 ]]; then
  echo "expected exit 37 (3*10+7) for main.kl, got $ec" >&2
  exit 1
fi

# Alias-qualified type name (using pair = abi.qualified_type_name.pair; pair::Pair).
"$KINGLET" --check "$CASE/main_alias.kl" >/dev/null
set +e
"$KINGLET" "$CASE/main_alias.kl" >/dev/null
ec=$?
set -e
if [[ "$ec" -ne 42 ]]; then
  echo "expected exit 42 (4*10+2) for main_alias.kl, got $ec" >&2
  exit 1
fi

echo "Qualified type name ABI tests passed."

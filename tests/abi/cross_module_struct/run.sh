#!/usr/bin/env bash
# ADR 0023 D9 / T1: cross-module struct by-value copy and stable field offsets.
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

"$KINGLET" --check "$CASE/main.kl" >/dev/null
"$KINGLET" "$CASE/main.kl" >/dev/null
ec=$?
if [[ "$ec" -ne 37 ]]; then
  echo "expected exit 37 (3*10+7), got $ec" >&2
  exit 1
fi
echo "Cross-module struct ABI tests passed."

#!/usr/bin/env bash
# Bootstrap sema pass/fail smoke (ADR 0022 N3).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KINGLET="${KINGLET:-${KINGLET_BOOTSTRAP:-}}"
if [[ -z "$KINGLET" ]]; then
  for candidate in "$ROOT/out/Llvm/kinglet" "$ROOT/out/Debug/kinglet"; do
    [[ -x "$candidate" ]] && KINGLET="$candidate" && break
  done
fi
[[ -x "$KINGLET" ]] || { echo "kinglet not found" >&2; exit 2; }

for f in "$ROOT/tests/sema/pass"/*.kl; do
  [[ -f "$f" ]] || continue
  name=$(basename "$f")
  if ! "$KINGLET" --check "$f" >/dev/null 2>&1; then
    echo "FAIL sema/pass/$name expected OK" >&2
    exit 1
  fi
done

for f in "$ROOT/tests/sema/fail"/*.kl; do
  [[ -f "$f" ]] || continue
  name=$(basename "$f")
  if "$KINGLET" --check "$f" >/dev/null 2>&1; then
    echo "FAIL sema/fail/$name expected error" >&2
    exit 1
  fi
done

echo "Sema tests passed."

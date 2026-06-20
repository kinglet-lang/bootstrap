#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KINGLET="${KINGLET_BOOTSTRAP:-$ROOT/out/Default/kinglet}"
CASE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -x "$KINGLET" ]]; then
  echo "kinglet not found at $KINGLET" >&2
  exit 2
fi

"$KINGLET" --check "$CASE/main.kl"
"$KINGLET" --check "$CASE/app_ns.kl"
"$KINGLET" --check "$CASE/three_seg.kl"

if "$KINGLET" --check "$CASE/reject_using_block.kl" 2>/dev/null; then
  echo "Expected reject_using_block.kl to fail" >&2
  exit 1
fi
if "$KINGLET" --check "$CASE/reject_using_wildcard.kl" 2>/dev/null; then
  echo "Expected reject_using_wildcard.kl to fail" >&2
  exit 1
fi
if "$KINGLET" --check "$CASE/reject_using_selective_io.kl" 2>/dev/null; then
  echo "Expected reject_using_selective_io.kl to fail" >&2
  exit 1
fi
echo "Hierarchical module tests passed."

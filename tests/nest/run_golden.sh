#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${NEST_TEST_OUT:-$ROOT/out/Default}"

if [[ ! -x "$OUT/nest_parser_test" ]]; then
  echo "nest_parser_test not built; run: ninja -C out/Default nest_parser_test" >&2
  exit 2
fi

(cd "$OUT" && ./nest_parser_test)

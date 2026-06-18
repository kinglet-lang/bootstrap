#!/usr/bin/env bash
# Directory-as-module import: `import user;` auto-loads every user/*.kl as a
# submodule (user.math, user.extra, ...) with no _dir.kl manifest.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KINGLET="${KINGLET_BOOTSTRAP:-$ROOT/out/Default/kinglet}"
CASE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -x "$KINGLET" ]]; then
  echo "kinglet not found at $KINGLET" >&2
  exit 2
fi

# Must type-check with directory-resolved submodules.
"$KINGLET" --check "$CASE/main.kl" >/dev/null

# Must run and produce the expected output.
output="$("$KINGLET" "$CASE/main.kl")"
expected=$'5\n42\n15'
if [[ "$output" != "$expected" ]]; then
  echo "dir_import output mismatch:" >&2
  diff <(printf '%s' "$expected") <(printf '%s' "$output") >&2
  exit 1
fi

echo "Directory import tests passed."

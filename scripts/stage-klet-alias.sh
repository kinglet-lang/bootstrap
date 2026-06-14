#!/usr/bin/env bash
# Create a klet CLI alias beside kinglet in release archives (same binary, second name).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${1:?usage: stage-klet-alias.sh <dist-dir>}"

KINGLET=""
KLET=""
if [[ -f "$DIST/kinglet" ]]; then
  KINGLET="$DIST/kinglet"
  KLET="$DIST/klet"
elif [[ -f "$DIST/kinglet.exe" ]]; then
  KINGLET="$DIST/kinglet.exe"
  KLET="$DIST/klet.exe"
else
  echo "stage-klet-alias: missing kinglet in $DIST" >&2
  exit 1
fi

if [[ -e "$KLET" ]]; then
  rm -f "$KLET"
fi

if [[ "$KINGLET" == *.exe ]]; then
  if cmd //c mklink /H "$(cygpath -w "$KLET")" "$(cygpath -w "$KINGLET")" 2>/dev/null; then
    echo "staged klet.exe as hard link to kinglet.exe"
    exit 0
  fi
  cat >"$DIST/klet.cmd" <<EOF
@echo off
"%~dp0kinglet.exe" %*
EOF
  echo "staged klet.cmd -> kinglet.exe"
else
  ln -s "$(basename "$KINGLET")" "$KLET"
  echo "staged klet -> $(basename "$KINGLET")"
fi

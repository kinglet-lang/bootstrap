#!/usr/bin/env bash
# Create klet as a second name for the same kinglet binary (no duplicate link, no .cmd).
set -euo pipefail

DIST="${1:?usage: stage-klet-alias.sh <dist-dir>}"

if [[ -f "$DIST/kinglet.exe" ]]; then
  KINGLET="$DIST/kinglet.exe"
  KLET="$DIST/klet.exe"
  unix_mode=false
elif [[ -f "$DIST/kinglet" ]]; then
  KINGLET="$DIST/kinglet"
  KLET="$DIST/klet"
  unix_mode=true
else
  echo "stage-klet-alias: missing kinglet in $DIST" >&2
  exit 1
fi

rm -f "$KLET" "$DIST/klet.cmd" "$DIST/klet" 2>/dev/null || true

to_windows_path() {
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -w "$1"
  else
    printf '%s' "$1"
  fi
}

make_windows_hardlink() {
  local kinglet_win klet_win
  kinglet_win=$(to_windows_path "$KINGLET")
  klet_win=$(to_windows_path "$KLET")

  if cmd //c "mklink /H \"$klet_win\" \"$kinglet_win\"" >/dev/null 2>&1; then
    return 0
  fi
  if cmd //c "fsutil hardlink create \"$klet_win\" \"$kinglet_win\"" >/dev/null 2>&1; then
    return 0
  fi
  return 1
}

if [[ "$unix_mode" == false ]]; then
  if make_windows_hardlink; then
    echo "staged klet.exe as hard link to kinglet.exe"
    exit 0
  fi
  echo "stage-klet-alias: could not create klet.exe hard link; use kinglet.exe" >&2
  exit 1
fi

ln -sf "$(basename "$KINGLET")" "$KLET"
echo "staged klet -> $(basename "$KINGLET")"

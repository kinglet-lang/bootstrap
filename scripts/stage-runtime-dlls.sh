#!/usr/bin/env bash
# Stage the transitive closure of DLLs that a MinGW-built Windows executable
# imports (and that live in a source bin dir) into a distribution directory, so
# the binary runs without MSYS2/MinGW on PATH. Mirrors the Copy-RequiredDlls
# logic in scripts/build.ps1.
#
#   bash scripts/stage-runtime-dlls.sh <dist-dir> [src-bin] [exe-name]
#
# Default src-bin is /mingw64/bin (MSYS2 MinGW64, as used by release.yml);
# default exe-name is kinglet.exe. Walking the full import graph (not just the
# exe's direct imports) catches indirect deps such as libwinpthread, which
# libstdc++ pulls in. Non-fatal: a missing DLL is skipped, never fatal.
set -euo pipefail

DIST="${1:?usage: stage-runtime-dlls.sh <dist-dir> [src-bin] [exe-name]}"
SRC="${2:-/mingw64/bin}"
EXE_NAME="${3:-kinglet.exe}"
OBJDUMP="${OBJDUMP:-${SRC}/objdump.exe}"

if [ ! -x "$OBJDUMP" ]; then
  OBJDUMP="$(command -v objdump || command -v objdump.exe || true)"
fi
if [ -z "$OBJDUMP" ]; then
  echo "stage-runtime-dlls: objdump not found; skipping DLL staging" >&2
  exit 0
fi

imports_of() {
  "$OBJDUMP" -p "$1" 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p'
}

declare -A seen=()
queue=("${DIST}/${EXE_NAME}")
while [ "${#queue[@]}" -gt 0 ]; do
  cur="${queue[0]}"
  queue=("${queue[@]:1}")
  [ -f "$cur" ] || continue
  while IFS= read -r dll; do
    [ -n "$dll" ] || continue
    [ -z "${seen[$dll]:-}" ] || continue
    if [ -f "${SRC}/${dll}" ]; then
      cp -f "${SRC}/${dll}" "${DIST}/${dll}"
      seen["$dll"]=1
      queue+=("${DIST}/${dll}") # follow this DLL's own imports
    fi
  done < <(imports_of "$cur")
done

echo "staged ${#seen[@]} runtime DLL(s) into ${DIST}"

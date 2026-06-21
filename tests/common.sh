#!/usr/bin/env bash
# Shared helpers for bootstrap test suites (decision 0012, bootstrap profile).

resolve_kinglet_bin() {
  local p="$1"
  if [[ -x "$p" ]]; then
    printf '%s' "$p"
    return 0
  fi
  if [[ -x "${p}.exe" ]]; then
    printf '%s' "${p}.exe"
    return 0
  fi
  printf '%s' "$p"
}

# Resolve the bootstrap kinglet binary. Prints absolute path on stdout.
resolve_kinglet() {
  local root="$1"
  local k="${KINGLET:-${KINGLET_BOOTSTRAP:-}}"
  local candidate

  if [[ -n "$k" ]]; then
    k="$(resolve_kinglet_bin "$k")"
    if [[ -x "$k" ]]; then
      printf '%s' "$(cd "$(dirname "$k")" && pwd)/$(basename "$k")"
      return 0
    fi
  fi

  for candidate in \
    "$root/out/Debug/kinglet" \
    "$root/out/Default/kinglet" \
    "$root/out/Llvm/kinglet" \
    "$root/out/Release/kinglet"; do
    candidate="$(resolve_kinglet_bin "$candidate")"
    if [[ -x "$candidate" ]]; then
      printf '%s' "$(cd "$(dirname "$candidate")" && pwd)/$(basename "$candidate")"
      return 0
    fi
  done

  echo "bootstrap kinglet not found (set KINGLET or build with ninja -C out/Debug)" >&2
  return 2
}

export_kinglet() {
  local root="$1"
  export KINGLET="$(resolve_kinglet "$root")" || return 2
  export KINGLET_BOOTSTRAP="$KINGLET"
}

# Normalize CRLF to LF byte-for-byte in captured outputs.
strip_cr() {
  local f
  for f in "$@"; do
    [[ -f "$f" ]] || continue
    tr -d '\r' <"$f" >"$f.nocr" && mv -f "$f.nocr" "$f"
  done
}

maybe_rebuild_kinglet() {
  local root="$1"
  if [[ "${KINGLET_SKIP_REBUILD:-}" == "1" ]]; then
    return 0
  fi
  local out_dir
  out_dir="$(cd "$(dirname "$KINGLET")" && pwd)"
  ninja -C "$out_dir" kinglet >/dev/null 2>&1 || true
}

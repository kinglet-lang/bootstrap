#!/usr/bin/env bash
# Fetch pinned GN + Ninja into ./tools for reproducible builds.
#
#   bash scripts/bootstrap.sh
#   source tools/env.sh          # then `gn` and `ninja` resolve from ./tools/bin
#
# GN has no semver (CIPD, very stable). Ninja is pinned to an exact release.
# The tools land in ./tools (gitignored); tools/env.sh wires PATH.
#
# LLVM is NOT fetched by this script — install it via your system package
# manager (see docs/BUILD.md for supported versions).

set -euo pipefail

NINJA_VERSION="1.12.1"
GN_CIPD_VERSION="latest"   # GN has no semver; CIPD instance id or "latest"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLS="$ROOT/tools"
BIN="$TOOLS/bin"

info() { printf '%s\n' "$*" >&2; }

http_get() {
  if command -v curl >/dev/null 2>&1; then
    curl -fSL "$1" -o "$2"
  elif command -v wget >/dev/null 2>&1; then
    wget -qO "$2" "$1"
  else
    echo "need curl or wget to download" >&2; exit 1
  fi
}

detect_platform() {
  local uname_s uname_m
  uname_s="$(uname -s)"
  uname_m="$(uname -m)"
  case "$uname_s" in
    Darwin)
      case "$uname_m" in
        arm64|aarch64) echo "darwin-arm64" ;;
        x86_64)        echo "darwin-x86_64" ;;
        *) echo "unsupported macOS arch '$uname_m'" >&2; exit 1 ;;
      esac
      ;;
    Linux)
      case "$uname_m" in
        x86_64|amd64) echo "linux-x86_64" ;;
        *) echo "unsupported Linux arch '$uname_m' (only x86_64 supported)" >&2; exit 1 ;;
      esac
      ;;
    MINGW*|MSYS*|CYGWIN*)
      echo "this script is for Unix; on Windows use scripts/bootstrap.ps1" >&2; exit 1
      ;;
    *)
      echo "unsupported OS '$uname_s'" >&2; exit 1
      ;;
  esac
}

gn_plat_for() {
  case "$1" in
    darwin-arm64) echo "mac-arm64" ;;
    darwin-x86_64) echo "mac-amd64" ;;
    linux-x86_64)  echo "linux-amd64" ;;
  esac
}

ninja_plat_for() {
  case "$1" in
    darwin-*) echo "mac" ;;
    linux-*)  echo "linux" ;;
  esac
}

install_gn() {
  local plat="$1" gn_plat
  gn_plat="$(gn_plat_for "$plat")"
  info "GN ($gn_plat, $GN_CIPD_VERSION)"
  http_get "https://chrome-infra-packages.appspot.com/dl/gn/gn/$gn_plat/+/$GN_CIPD_VERSION" "$TOOLS/gn.zip"
  unzip -o "$TOOLS/gn.zip" -d "$BIN" >/dev/null
  rm -f "$TOOLS/gn.zip"
  chmod +x "$BIN/gn"
}

install_ninja() {
  local plat="$1" ninja_plat
  ninja_plat="$(ninja_plat_for "$plat")"
  info "Ninja ($NINJA_VERSION)"
  http_get "https://github.com/ninja-build/ninja/releases/download/v$NINJA_VERSION/ninja-$ninja_plat.zip" "$TOOLS/ninja.zip"
  unzip -o "$TOOLS/ninja.zip" -d "$BIN" >/dev/null
  rm -f "$TOOLS/ninja.zip"
  chmod +x "$BIN/ninja"
}

write_env() {
  {
    echo '# Source from the repo root:  source tools/env.sh'
    echo 'export PATH="$PWD/tools/bin:$PATH"'
  } > "$TOOLS/env.sh"
  info "wrote tools/env.sh"
}

main() {
  local plat
  plat="$(detect_platform)"
  mkdir -p "$BIN"

  install_gn "$plat"
  install_ninja "$plat"
  write_env

  info ""
  info "Done. GN + Ninja are in tools/bin."
  info "Next steps:"
  info "  source tools/env.sh"
  info ""
  info "LLVM note: if you need the native backend, install LLVM via your system"
  info "package manager (see docs/BUILD.md for supported versions), then:"
  info '  gn gen out/Default --args='"'"'is_debug=false enable_llvm=true llvm_config="$(which llvm-config)"'"'"
  info '  ninja -C out/Default kinglet kinglet_rt'
  info ""
}
main "$@"

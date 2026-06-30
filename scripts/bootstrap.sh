#!/usr/bin/env bash
# Fetch pinned GN + Ninja and build LLVM from source into ./tools, so every
# developer and CI runner uses the same toolchain — built for the host it runs
# on (no prebuilt system-library mismatch).
#
#   bash scripts/bootstrap.sh
#   source tools/env.sh          # then `gn`, `ninja`, `llvm-config` resolve from ./tools
#
# GN has no semver (CIPD, very stable). Ninja is pinned to an exact release.
# LLVM is pinned via the llvm-project git tag and BUILT FROM SOURCE — the first
# run takes ~20–40 min; tools/llvm is reused thereafter (and cached by CI).
# Requires `cmake` and `git` on the host (preinstalled on GitHub runners and
# most dev machines). The tools land in ./tools (gitignored); tools/env.sh wires
# PATH and LLVM_CONFIG so build/scripts/find_llvm_config.py picks it up first.

set -euo pipefail

NINJA_VERSION="1.12.1"
LLVM_VERSION="22.1.8"
GN_CIPD_VERSION="latest"   # GN has no semver; CIPD instance id or "latest"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLS="$ROOT/tools"
BIN="$TOOLS/bin"
LLVM="$TOOLS/llvm"

info() { printf '%s\n' "$*" >&2; }

http_get() {
  # http_get <url> <out>
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

# Build LLVM from the pinned source tag into tools/llvm. Idempotent: if
# tools/llvm/bin/llvm-config already exists, the build is skipped (cache-friendly).
build_llvm() {
  if [ -x "$LLVM/bin/llvm-config" ]; then
    info "LLVM: tools/llvm already installed, skipping build"
    return 0
  fi
  local src="$TOOLS/llvm-src" build_dir="$TOOLS/llvm-build"
  info "LLVM: cloning llvm-project $LLVM_VERSION (depth 1)"
  git clone --depth 1 --branch "llvmorg-$LLVM_VERSION" https://github.com/llvm/llvm-project.git "$src"
  info "LLVM: cmake configure (Release; targets X86+AArch64; no clang/lld/tests)"
  cmake -G Ninja \
    -S "$src/llvm" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_PROJECTS="" \
    -DLLVM_TARGETS_TO_BUILD="X86;AArch64" \
    -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_BENCHMARKS=OFF \
    -DLLVM_INCLUDE_DOCS=OFF \
    -DLLVM_ENABLE_ZSTD=OFF \
    -DLLVM_ENABLE_LIBEDIT=OFF \
    -DLLVM_ENABLE_LIBXML2=OFF \
    -DLLVM_ENABLE_TERMINFO=OFF \
    -DCMAKE_INSTALL_PREFIX="$LLVM" \
    -DCMAKE_MAKE_PROGRAM="$BIN/ninja"
  info "LLVM: building + installing (first run takes ~20–40 minutes)"
  "$BIN/ninja" -C "$build_dir" install
}

write_env() {
  {
    echo '# Source from the repo root:  source tools/env.sh'
    echo 'export PATH="$PWD/tools/bin:$PATH"'
    echo 'export LLVM_CONFIG="$PWD/tools/llvm/bin/llvm-config"'
  } > "$TOOLS/env.sh"
  info "wrote tools/env.sh"
}

main() {
  for tool in unzip cmake git; do
    command -v "$tool" >/dev/null 2>&1 || { echo "required tool not found: $tool" >&2; exit 1; }
  done

  local plat
  plat="$(detect_platform)"
  mkdir -p "$BIN"

  install_gn "$plat"
  install_ninja "$plat"
  build_llvm
  write_env

  info ""
  info "Next steps:"
  info "  source tools/env.sh"
  info '  gn gen out/Default --args='"'"'is_debug=false enable_llvm=true llvm_config="$PWD/tools/llvm/bin/llvm-config"'"'"
  info '  ninja -C out/Default kinglet kinglet_rt'
  info ""
}
main "$@"

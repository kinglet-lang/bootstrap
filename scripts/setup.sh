#!/usr/bin/env bash
# One-shot dev-environment setup: pinned GN + Ninja, and LLVM detection
# (with optional install). Use this before any GN/ninja invocation.
#
#   bash scripts/setup.sh              # GN + Ninja + detect LLVM, print path
#   source scripts/setup.sh            # same, but also export LLVM_CONFIG
#   bash scripts/setup.sh --install    # also install LLVM if missing
#
# GN has no semver (CIPD, very stable). Ninja is pinned to an exact release.
# The tools land in ./tools (gitignored). setup.sh wires ./tools/bin onto
# PATH for both the current shell and future shells (via the shell profile),
# so a separate `source tools/env.sh` step is no longer required — that file
# is still written for CI / non-interactive use.
#
# LLVM: the script searches PATH, /usr/lib/llvm-*/bin, and Homebrew prefixes.
# Set PREFERRED_LLVM (e.g. "16") to bias toward a specific version.
# Pass --install to attempt automatic installation (apt on Linux, brew on macOS).
#
# Set SETUP_SH_SKIP_MAIN=1 before sourcing to load helper functions
# (detect_platform, find_llvm_config, add_to_path, ...) without running
# main — used by scripts/build.sh to reuse this file's LLVM detection.

set -euo pipefail

# ========== configuration ==========

NINJA_VERSION="1.12.1"
GN_CIPD_VERSION="latest"   # GN has no semver; CIPD instance id or "latest"
PREFERRED_LLVM="${PREFERRED_LLVM:-}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLS="$ROOT/tools"
BIN="$TOOLS/bin"

DO_INSTALL=false

for arg in "$@"; do
  case "$arg" in
    --install) DO_INSTALL=true ;;
    --help|-h)
      echo "usage: setup.sh [--install]"
      echo "  --install   Also install LLVM if not already present"
      echo ""
      echo "Environment:"
      echo "  PREFERRED_LLVM=16   Prefer a specific major version"
      exit 0
      ;;
    *)
      echo "setup.sh: unknown option '$arg'" >&2
      exit 2
      ;;
  esac
done

info()  { printf '\033[34m>\033[0m %s\n' "$*" >&2; }
warn()  { printf '\033[33m!\033[0m %s\n' "$*" >&2; }
err()   { printf '\033[31m✗\033[0m %s\n' "$*" >&2; }

# ========== dependency check ==========

check_deps() {
  local missing=()
  command -v unzip >/dev/null 2>&1 || missing+=("unzip")
  command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1 || missing+=("curl or wget")
  if [[ ${#missing[@]} -gt 0 ]]; then
    err "missing required tools: ${missing[*]}"
    case "$(uname -s)" in
      Linux)  err "install with: sudo apt install -y unzip curl" ;;
      Darwin) err "install with: brew install unzip curl" ;;
    esac
    exit 1
  fi
}

# ========== platform helpers ==========

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
      echo "this script is for Unix; on Windows use scripts/setup.ps1" >&2; exit 1
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

http_get() {
  if command -v curl >/dev/null 2>&1; then
    curl -fSL "$1" -o "$2"
  elif command -v wget >/dev/null 2>&1; then
    wget -qO "$2" "$1"
  else
    echo "need curl or wget to download" >&2; exit 1
  fi
}

# ========== GN + Ninja ==========

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

# ========== user PATH wiring ==========
# Persist $BIN on PATH across shells, and export it into the current shell
# when this script is sourced. Mirrors scripts/install.sh's approach.

profile_for_shell() {
  case "${SHELL:-}" in
    */zsh)  echo "$HOME/.zshrc" ;;
    */bash) [[ -f "$HOME/.bashrc" ]] && echo "$HOME/.bashrc" || echo "$HOME/.bash_profile" ;;
    */fish) echo "$HOME/.config/fish/config.fish" ;;
    *)      echo "$HOME/.profile" ;;
  esac
}

add_bin_to_path() {
  [[ "${SETUP_SH_NO_MODIFY_PATH:-0}" == "1" ]] && return 0

  local profile
  profile="$(profile_for_shell)"
  mkdir -p "$(dirname "$profile")"

  local line
  case "$profile" in
    *config.fish) line="fish_add_path $BIN" ;;
    *)            line="export PATH=\"$BIN:\$PATH\"" ;;
  esac

  if ! { [[ -f "$profile" ]] && grep -qF "$BIN" "$profile"; }; then
    printf '\n# kinglet dev toolchain (scripts/setup.sh)\n%s\n' "$line" >> "$profile"
    info "added $BIN to PATH in $profile"
  fi

  # Also export into the current shell right away when sourced.
  case ":${PATH:-}:" in
    *":$BIN:"*) ;;
    *) export PATH="$BIN:$PATH" ;;
  esac
}

# ========== LLVM ==========

detect_os() {
  case "$(uname -s)" in
    Darwin) echo "macos" ;;
    Linux)  echo "linux" ;;
    *)      echo "unsupported" ;;
  esac
}

# Scan known paths for llvm-config. If PREFERRED_LLVM is set, bias toward
# that version; otherwise return the newest one found.
find_llvm_config() {
  local best=""
  local best_ver=0

  # 1. Honour explicit LLVM_CONFIG from the environment.
  if [[ -n "${LLVM_CONFIG:-}" && -x "$LLVM_CONFIG" ]]; then
    echo "$LLVM_CONFIG"
    return 0
  fi

  # 2. Check PATH for llvm-config or llvm-config-N.
  for candidate in llvm-config llvm-config-*; do
    local resolved
    resolved="$(command -v "$candidate" 2>/dev/null || true)"
    if [[ -n "$resolved" && -x "$resolved" ]]; then
      local ver
      ver="$("$resolved" --version 2>/dev/null | grep -oE '^[0-9]+' || echo 0)"
      if [[ "$ver" -gt "$best_ver" ]]; then
        best="$resolved"
        best_ver="$ver"
      fi
    fi
  done

  # 3. Check common installation prefixes.
  local prefixes=("/usr" "/usr/local" "/opt/homebrew/opt/llvm" "$(brew --prefix llvm 2>/dev/null || true)")
  for prefix in "${prefixes[@]}"; do
    [[ -z "$prefix" ]] && continue
    if [[ -n "$PREFERRED_LLVM" ]]; then
      local ver_cfg="${prefix}/lib/llvm-${PREFERRED_LLVM}/bin/llvm-config"
      if [[ -x "$ver_cfg" ]]; then
        echo "$ver_cfg"
        return 0
      fi
    fi
    for cfg in "${prefix}"/lib/llvm-*/bin/llvm-config "${prefix}"/opt/llvm-*/bin/llvm-config; do
      if [[ -x "$cfg" ]]; then
        local ver
        ver="$("$cfg" --version 2>/dev/null | grep -oE '^[0-9]+' || echo 0)"
        if [[ "$ver" -gt "$best_ver" ]]; then
          best="$cfg"
          best_ver="$ver"
        fi
      fi
    done
  done

  if [[ -n "$best" ]]; then
    echo "$best"
    return 0
  fi
  return 1
}

install_llvm_linux() {
  local ver="${PREFERRED_LLVM:-16}"
  info "installing LLVM $ver (apt)"

  local codename
  codename="$(. /etc/os-release && echo "${VERSION_CODENAME:-}")"
  if [[ -z "$codename" ]]; then
    codename="$(lsb_release -cs 2>/dev/null || echo "bookworm")"
  fi

  local sources_file="/etc/apt/sources.list.d/llvm.list"
  if [[ ! -f "$sources_file" ]]; then
    sudo apt-get update -qq
    sudo apt-get install -y -qq lsb-release wget software-properties-common gnupg
    wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc >/dev/null
    sudo tee "$sources_file" >/dev/null <<APTEOF
deb http://apt.llvm.org/${codename}/ llvm-toolchain-${codename}-${ver} main
deb-src http://apt.llvm.org/${codename}/ llvm-toolchain-${codename}-${ver} main
APTEOF
  fi

  sudo apt-get update -qq
  sudo apt-get install -y -qq "llvm-${ver}-dev" "clang-${ver}" "lld-${ver}"

  local cfg="/usr/lib/llvm-${ver}/bin/llvm-config"
  if [[ -x "$cfg" ]]; then
    echo "$cfg"
  else
    err "LLVM $ver installed but llvm-config not found at $cfg"
    return 1
  fi
}

install_llvm_macos() {
  info "installing LLVM via Homebrew"
  if ! command -v brew >/dev/null 2>&1; then
    err "Homebrew is not installed. Install it first: https://brew.sh"
    return 1
  fi
  brew install llvm >/dev/null

  local cfg
  cfg="$(brew --prefix llvm 2>/dev/null || true)/bin/llvm-config"
  if [[ -x "$cfg" ]]; then
    echo "$cfg"
  else
    cfg="$(find /opt/homebrew/opt/llvm -name llvm-config -type f 2>/dev/null | head -1)"
    if [[ -n "$cfg" ]]; then
      echo "$cfg"
    else
      err "brew install llvm succeeded but llvm-config not found"
      return 1
    fi
  fi
}

setup_llvm() {
  local os

  # Find existing.
  local cfg
  if cfg="$(find_llvm_config)"; then
    info "found llvm-config: $cfg"
    echo "$cfg"
    return 0
  fi

  if ! $DO_INSTALL; then
    warn "no LLVM installation found (pass --install to install automatically)"
    return 1
  fi

  os="$(detect_os)"
  case "$os" in
    linux)
      cfg="$(install_llvm_linux)"
      ;;
    macos)
      cfg="$(install_llvm_macos)"
      ;;
    *)
      err "unsupported OS for automatic LLVM install"
      return 1
      ;;
  esac

  echo "$cfg"
}

# ========== main ==========

main() {
  info "Kinglet dev setup"
  info ""

  check_deps

  local plat
  plat="$(detect_platform)"
  mkdir -p "$BIN"

  # GN + Ninja (always).
  install_gn "$plat"
  install_ninja "$plat"
  write_env
  add_bin_to_path
  info ""

  # LLVM (detect, optionally install).
  local llvm_cfg
  if llvm_cfg="$(setup_llvm)"; then
    export LLVM_CONFIG="$llvm_cfg"
    info "LLVM ready — gn gen --args='enable_llvm=true llvm_config=\"$llvm_cfg\"'"
  else
    warn "LLVM not available — native backend disabled"
    info "re-run with --install or set LLVM_CONFIG / PREFERRED_LLVM"
  fi

  info ""
  info "Done. gn/ninja are on PATH now (new shells too). Next:"
  if [[ -n "${LLVM_CONFIG:-}" ]]; then
    info "  bash scripts/build.sh"
  else
    info "  gn gen out/Default --args='is_debug=false'"
    info "  ninja -C out/Default kinglet"
  fi

  # When not sourced, emit LLVM_CONFIG on stdout so callers can capture it.
  if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "${LLVM_CONFIG:-}"
  fi
}

# Allow other scripts to `source scripts/setup.sh` purely to reuse the helper
# functions (detect_platform, find_llvm_config, profile_for_shell, ...)
# without running the full install flow.
if [[ "${SETUP_SH_SKIP_MAIN:-0}" != "1" ]]; then
  # When sourced, run main and export LLVM_CONFIG into the caller's environment.
  if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
  else
    main "$@"
    if [[ -n "${LLVM_CONFIG:-}" ]]; then
      export LLVM_CONFIG
    fi
  fi
fi

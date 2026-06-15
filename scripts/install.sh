#!/bin/sh
# Kinglet installer — fetch a prebuilt release and put `kinglet` on PATH.
#
#   curl -fsSL https://raw.githubusercontent.com/kinglet-lang/bootstrap/main/scripts/install.sh | sh
#
# Environment overrides:
#   KINGLET_VERSION       Tag to install (e.g. v0.0.6). Default: latest release.
#   KINGLET_INSTALL_DIR   Install prefix. Default: $HOME/.kinglet
#   KINGLET_REPO          GitHub owner/repo. Default: kinglet-lang/bootstrap
#   KINGLET_BASE_URL      Override download base (mirror/internal). Default:
#                         https://github.com/$REPO/releases/download/$version
#   KINGLET_API_URL       Override latest-release API endpoint.
#   KINGLET_NO_MODIFY_PATH=1  Skip writing PATH lines to shell profiles.
#
# POSIX sh only (no bashisms) so it runs under the piped `sh`.

set -eu

REPO="${KINGLET_REPO:-kinglet-lang/bootstrap}"
INSTALL_DIR="${KINGLET_INSTALL_DIR:-$HOME/.kinglet}"
BIN_DIR="$INSTALL_DIR/bin"

# ---- output helpers ---------------------------------------------------------

if [ -t 1 ]; then
  C_BOLD="$(printf '\033[1m')"
  C_DIM="$(printf '\033[2m')"
  C_RED="$(printf '\033[31m')"
  C_GRN="$(printf '\033[32m')"
  C_YLW="$(printf '\033[33m')"
  C_RST="$(printf '\033[0m')"
else
  C_BOLD='' C_DIM='' C_RED='' C_GRN='' C_YLW='' C_RST=''
fi

info() { printf '%s%s%s\n' "$C_DIM" "$1" "$C_RST" >&2; }
note() { printf '%s%s%s\n' "$C_BOLD" "$1" "$C_RST" >&2; }
warn() { printf '%swarning:%s %s\n' "$C_YLW" "$C_RST" "$1" >&2; }
ok()   { printf '%s%s%s\n' "$C_GRN" "$1" "$C_RST" >&2; }
err()  { printf '%serror:%s %s\n' "$C_RED" "$C_RST" "$1" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || err "required tool not found: $1"; }

# ---- platform detection -----------------------------------------------------

detect_target() {
  uname_s="$(uname -s)"
  uname_m="$(uname -m)"
  case "$uname_s" in
    Darwin)
      case "$uname_m" in
        arm64|aarch64) echo "kinglet-macos-arm64" ;;
        *) err "unsupported macOS arch '$uname_m' (only arm64 is published)" ;;
      esac
      ;;
    Linux)
      case "$uname_m" in
        x86_64|amd64) echo "kinglet-linux-x64" ;;
        *) err "unsupported Linux arch '$uname_m' (only x86_64 is published)" ;;
      esac
      ;;
    MINGW*|MSYS*|CYGWIN*)
      err "Windows detected: download kinglet-windows-x64.tar.gz from https://github.com/$REPO/releases or use Scoop/winget"
      ;;
    *)
      err "unsupported OS '$uname_s'"
      ;;
  esac
}

# ---- download helpers -------------------------------------------------------

http_get() {
  # http_get <url> <out-file>
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$1" -o "$2"
  elif command -v wget >/dev/null 2>&1; then
    wget -qO "$2" "$1"
  else
    err "need curl or wget to download"
  fi
}

resolve_version() {
  if [ -n "${KINGLET_VERSION:-}" ]; then
    echo "$KINGLET_VERSION"
    return 0
  fi
  api="${KINGLET_API_URL:-https://api.github.com/repos/$REPO/releases/latest}"
  tmp="$(mktemp)"
  http_get "$api" "$tmp" || err "failed to query latest release"
  tag="$(sed -n 's/.*"tag_name"[ ]*:[ ]*"\([^"]*\)".*/\1/p' "$tmp" | head -n1)"
  rm -f "$tmp"
  [ -n "$tag" ] || err "could not determine latest release tag (set KINGLET_VERSION)"
  echo "$tag"
}

verify_checksum() {
  # verify_checksum <archive> <sums-file> <archive-basename>
  archive="$1"; sums="$2"; name="$3"
  expected="$(grep -E "[[:space:]]\*?$name\$" "$sums" 2>/dev/null | awk '{print $1}' | head -n1)"
  [ -n "$expected" ] || { warn "no checksum entry for $name; skipping verification"; return 0; }
  if command -v sha256sum >/dev/null 2>&1; then
    actual="$(sha256sum "$archive" | awk '{print $1}')"
  elif command -v shasum >/dev/null 2>&1; then
    actual="$(shasum -a 256 "$archive" | awk '{print $1}')"
  else
    warn "no sha256 tool found; skipping verification"
    return 0
  fi
  [ "$actual" = "$expected" ] || err "checksum mismatch for $name (expected $expected, got $actual)"
  info "checksum verified"
}

# ---- PATH wiring ------------------------------------------------------------

profile_for_shell() {
  case "${SHELL:-}" in
    */zsh)  echo "$HOME/.zshrc" ;;
    */bash) [ -f "$HOME/.bashrc" ] && echo "$HOME/.bashrc" || echo "$HOME/.bash_profile" ;;
    */fish) echo "$HOME/.config/fish/config.fish" ;;
    *)      echo "$HOME/.profile" ;;
  esac
}

add_to_path() {
  case ":$PATH:" in
    *":$BIN_DIR:"*) return 0 ;;
  esac
  [ "${KINGLET_NO_MODIFY_PATH:-0}" = "1" ] && return 0

  profile="$(profile_for_shell)"
  mkdir -p "$(dirname "$profile")"
  case "$profile" in
    *config.fish)
      line="fish_add_path $BIN_DIR"
      ;;
    *)
      line="export PATH=\"$BIN_DIR:\$PATH\""
      ;;
  esac
  if ! { [ -f "$profile" ] && grep -qF "$BIN_DIR" "$profile"; }; then
    printf '\n# kinglet\n%s\n' "$line" >> "$profile"
    info "added $BIN_DIR to PATH in $profile"
  fi
  PROFILE_TOUCHED="$profile"
}

# ---- main -------------------------------------------------------------------

main() {
  need uname
  need mktemp
  need tar

  target="$(detect_target)"
  version="$(resolve_version)"
  archive_name="$target.tar.gz"
  base="${KINGLET_BASE_URL:-https://github.com/$REPO/releases/download/$version}"

  note "Installing kinglet $version ($target)"

  tmpdir="$(mktemp -d)"
  trap 'rm -rf "$tmpdir"' EXIT INT TERM

  archive="$tmpdir/$archive_name"
  info "downloading $base/$archive_name"
  http_get "$base/$archive_name" "$archive" || err "download failed: $base/$archive_name"

  sums="$tmpdir/SHA256SUMS"
  if http_get "$base/SHA256SUMS" "$sums" 2>/dev/null; then
    verify_checksum "$archive" "$sums" "$archive_name"
  else
    warn "SHA256SUMS not published for $version; skipping verification"
  fi

  info "extracting to $BIN_DIR"
  mkdir -p "$BIN_DIR"
  tar -xzf "$archive" -C "$BIN_DIR"
  chmod +x "$BIN_DIR/kinglet" 2>/dev/null || true
  [ -e "$BIN_DIR/klet" ] && chmod +x "$BIN_DIR/klet" 2>/dev/null || true

  [ -x "$BIN_DIR/kinglet" ] || err "install looks incomplete: $BIN_DIR/kinglet missing"

  PROFILE_TOUCHED=""
  add_to_path

  installed_version="$("$BIN_DIR/kinglet" --version 2>/dev/null || echo "$version")"
  ok "Installed: $installed_version -> $BIN_DIR/kinglet"

  # The native (LLVM) backend shells out to a C/C++ compiler to link programs.
  if ! command -v clang++ >/dev/null 2>&1 && ! command -v cc >/dev/null 2>&1; then
    warn "no 'clang++'/'cc' found — the native backend ('kinglet --native', 'kinglet build') needs one to link programs."
    warn "  macOS: xcode-select --install    Debian/Ubuntu: sudo apt install clang"
  fi

  echo >&2
  if [ -n "$PROFILE_TOUCHED" ]; then
    note "Restart your shell or run:  . \"$PROFILE_TOUCHED\""
  else
    case ":$PATH:" in
      *":$BIN_DIR:"*) : ;;
      *) note "Add to PATH:  export PATH=\"$BIN_DIR:\$PATH\"" ;;
    esac
  fi
  note "Verify with:  kinglet --version"
}

main "$@"

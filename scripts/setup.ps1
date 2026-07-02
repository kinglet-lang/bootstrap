# One-shot dev-environment setup for Windows: pinned GN + Ninja.
#
#   pwsh -File scripts/setup.ps1        # GN + Ninja, wire ./tools/bin onto PATH
#
# Windows mirrors the CI policy: GN + Ninja only, no LLVM — builds are
# compile-only (the native LLVM backend is not supported on Windows yet).
# The tools land in .\tools (gitignored). setup.ps1 wires .\tools\bin onto
# PATH for both the current session and future shells (via the PowerShell
# profile), so a separate `. .\tools\env.ps1` step is no longer required —
# that file is still written for CI / non-interactive use.
#
# Set SETUP_PS_NO_MODIFY_PATH=1 to skip persisting PATH to the profile.

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$NINJA_VERSION   = "1.12.1"
$GN_CIPD_VERSION = "latest"   # GN has no semver; CIPD instance id or "latest"

$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$ROOT       = Split-Path -Parent $SCRIPT_DIR
$TOOLS      = Join-Path $ROOT "tools"
$BIN        = Join-Path $TOOLS "bin"

function Info($msg) { Write-Host $msg -ForegroundColor DarkGray }
function Warn($msg) { Write-Host $msg -ForegroundColor Yellow }

# ── GN + Ninja ───────────────────────────────────────────────────────────────

function Install-Gn {
  Info "GN (windows-amd64, $GN_CIPD_VERSION)"
  $url = "https://chrome-infra-packages.appspot.com/dl/gn/gn/windows-amd64/+/$GN_CIPD_VERSION"
  $zip = Join-Path $TOOLS "gn.zip"
  Invoke-WebRequest -Uri $url -OutFile $zip
  Expand-Archive -Path $zip -DestinationPath $BIN -Force
  Remove-Item $zip
}

function Install-Ninja {
  Info "Ninja ($NINJA_VERSION)"
  $url = "https://github.com/ninja-build/ninja/releases/download/v$NINJA_VERSION/ninja-win.zip"
  $zip = Join-Path $TOOLS "ninja.zip"
  Invoke-WebRequest -Uri $url -OutFile $zip
  Expand-Archive -Path $zip -DestinationPath $BIN -Force
  Remove-Item $zip
}

function Write-Env {
  $lines = @(
    '# Dot-source from the repo root:  . .\tools\env.ps1',
    '$env:PATH = "$PWD\tools\bin;$env:PATH"'
  )
  ($lines -join "`r`n") | Set-Content -Path (Join-Path $TOOLS "env.ps1") -Encoding ascii
  Info "wrote tools\env.ps1"
}

# ── user PATH wiring ──────────────────────────────────────────────────────────
# Persist $BIN on PATH across shells (via the PowerShell profile), and export
# it into the current session right away. Mirrors scripts/setup.sh.

function Add-BinToPath {
  if ($env:SETUP_PS_NO_MODIFY_PATH -eq "1") { return }

  $profilePath = $PROFILE.CurrentUserAllHosts
  $profileDir  = Split-Path -Parent $profilePath
  if (-not (Test-Path $profileDir)) {
    New-Item -ItemType Directory -Force -Path $profileDir | Out-Null
  }

  $line = "`$env:PATH = `"$BIN;`$env:PATH`""
  $already = (Test-Path $profilePath) -and
             (Select-String -Path $profilePath -SimpleMatch $BIN -Quiet)
  if (-not $already) {
    Add-Content -Path $profilePath -Value "`r`n# kinglet dev toolchain (scripts/setup.ps1)`r`n$line"
    Info "added $BIN to PATH in $profilePath"
  }

  # Also export into the current session right away.
  if (($env:PATH -split ';') -notcontains $BIN) {
    $env:PATH = "$BIN;$env:PATH"
  }
}

# ── main ───────────────────────────────────────────────────────────────────

New-Item -ItemType Directory -Force -Path $BIN | Out-Null

Info "Kinglet dev setup (Windows)"
Info "LLVM: skipped on Windows (compile-only; native backend not supported here)"
Install-Gn
Install-Ninja
Write-Env
Add-BinToPath

Info ""
Info "Done. gn/ninja are on PATH now (new shells too). Next:"
Info "  pwsh -File scripts/build.ps1"
Info "or manually:"
Info '  gn gen out/Debug --args="is_debug=false"'
Info "  ninja -C out/Debug kinglet"
Info ""

# Windows toolchain setup complete.


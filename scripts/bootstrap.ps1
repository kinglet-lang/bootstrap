# Download pinned GN / Ninja into .\tools for reproducible builds (Windows).
#
#   pwsh -File scripts/bootstrap.ps1
#   .\tools\env.ps1                # then `gn` and `ninja` resolve from .\tools
#
# Windows mirrors the CI policy: GN + Ninja only, no LLVM — builds are
# compile-only (the native LLVM backend is not supported on Windows yet).
# The tools land in .\tools (gitignored).

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
    '# Dot-source from the repo root:  .\tools\env.ps1',
    '$env:PATH = "$PWD\tools\bin;$env:PATH"'
  )
  ($lines -join "`r`n") | Set-Content -Path (Join-Path $TOOLS "env.ps1") -Encoding ascii
  Info "wrote tools\env.ps1"
}

New-Item -ItemType Directory -Force -Path $BIN | Out-Null

Info "LLVM: skipped on Windows (compile-only; native backend not supported here)"
Install-Gn
Install-Ninja
Write-Env

Info ""
Info "Next steps:"
Info "  .\tools\env.ps1"
Info '  gn gen out/Debug --args="is_debug=false"'
Info "  ninja -C out/Debug kinglet"
Info ""

# Build the kinglet compiler on Windows and wire the binary onto PATH.
#
#   pwsh -File scripts/build.ps1                 # gn gen + ninja (release)
#   pwsh -File scripts/build.ps1 -DebugBuild     # debug build (-g, no -O2)
#   pwsh -File scripts/build.ps1 -Out out\Foo    # custom output dir
#
# Windows is compile-only: the native LLVM backend is not supported here, so
# this builds `kinglet` (no `kinglet_rt`). Requires scripts/setup.ps1 to have
# been run at least once (for GN + Ninja under .\tools\bin).
#
# After a successful build, kinglet.exe (and klet.exe, its alias) are staged
# into .\tools\bin and added to PATH via the same profile mechanism as setup.ps1.

[CmdletBinding()]
param(
  [switch]$DebugBuild,
  [string]$Out = "out\Default"
)

$ErrorActionPreference = "Stop"

$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$ROOT       = Split-Path -Parent $SCRIPT_DIR
$TOOLS      = Join-Path $ROOT "tools"
$BIN        = Join-Path $TOOLS "bin"

function Info($msg) { Write-Host $msg -ForegroundColor DarkGray }
function Warn($msg) { Write-Host $msg -ForegroundColor Yellow }
function Fail($msg) { Write-Host $msg -ForegroundColor Red; exit 1 }

Set-Location $ROOT

# ========== prerequisites ==========

$gn    = Join-Path $BIN "gn.exe"
$ninja = Join-Path $BIN "ninja.exe"
if (-not (Test-Path $gn) -or -not (Test-Path $ninja)) {
  Fail "GN/Ninja not found in $BIN`nrun 'pwsh -File scripts/setup.ps1' first (one-time toolchain setup)"
}
if (($env:PATH -split ';') -notcontains $BIN) {
  $env:PATH = "$BIN;$env:PATH"
}

# ========== configure ==========

$isDebug = if ($DebugBuild) { "true" } else { "false" }
$gnArgs  = "is_debug=$isDebug"

Info "gn gen $Out --args=`"$gnArgs`""
& $gn gen $Out --args="$gnArgs"
if ($LASTEXITCODE -ne 0) { Fail "gn gen failed" }

# ========== build ==========

Info "ninja -C $Out kinglet"
& $ninja -C $Out kinglet
if ($LASTEXITCODE -ne 0) { Fail "ninja build failed" }

$builtBin = Join-Path $ROOT (Join-Path $Out "kinglet.exe")
if (-not (Test-Path $builtBin)) {
  Fail "build finished but $builtBin is missing"
}

# ========== stage kinglet/klet + wire PATH ==========

New-Item -ItemType Directory -Force -Path $BIN | Out-Null
Copy-Item -Force $builtBin (Join-Path $BIN "kinglet.exe")

$aliasScript = Join-Path $SCRIPT_DIR "stage-klet-alias.ps1"
if (Test-Path $aliasScript) {
  try {
    & pwsh -NoProfile -File $aliasScript $BIN
  } catch {
    Warn "klet alias staging failed (non-fatal): $($_.Exception.Message)"
  }
}

# Persist $BIN on PATH across shells (mirrors setup.ps1's Add-BinToPath).
if ($env:SETUP_PS_NO_MODIFY_PATH -ne "1") {
  $profilePath = $PROFILE.CurrentUserAllHosts
  $profileDir  = Split-Path -Parent $profilePath
  if (-not (Test-Path $profileDir)) {
    New-Item -ItemType Directory -Force -Path $profileDir | Out-Null
  }
  $line = "`$env:PATH = `"$BIN;`$env:PATH`""
  $already = (Test-Path $profilePath) -and
             (Select-String -Path $profilePath -SimpleMatch $BIN -Quiet)
  if (-not $already) {
    Add-Content -Path $profilePath -Value "`r`n# kinglet dev toolchain (scripts/build.ps1)`r`n$line"
    Info "added $BIN to PATH in $profilePath"
  }
}

Info ""
Info "Done: $BIN\kinglet.exe"
& (Join-Path $BIN "kinglet.exe") --version 2>$null
Info "Restart your shell, or it's already on PATH for this session."

# One-shot dev-environment setup for Windows: pinned GN + Ninja (+ LLVM detect).
#
#   pwsh -File scripts/setup.ps1        # GN + Ninja, wire ./tools/bin onto PATH
#
# Windows mirrors scripts/setup.sh: GN + Ninja are fetched, and an LLVM
# installation is detected (and reported). When LLVM is available, the native
# backend is built; otherwise the build is compile-only. The tools land in
# .\tools (gitignored). setup.ps1 wires .\tools\bin onto PATH for both the
# current session and future shells (via the PowerShell profile), so a separate
# `. .\tools\env.ps1` step is no longer required — that file is still written
# for CI / non-interactive use.
#
# LLVM on Windows: the only complete, widely available distribution that ships
# llvm-config together with the matching libraries and headers is the MSYS2
# MinGW LLVM (e.g. C:\msys64\mingw64). scripts/build.ps1 wires the build to use
# that same MinGW clang++ toolchain so the LLVM libraries link correctly. The
# official llvm.org installer omits llvm-config and the dev libraries.
#
# Set SETUP_PS_NO_MODIFY_PATH=1 to skip persisting PATH to the profile.
#
# Other scripts dot-source this file solely to reuse Find-LlvmConfig / Add-BinToPath;
# in that case Invoke-Setup is not run.

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

# ========== GN + Ninja ==========

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

# ========== user PATH wiring ==========
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

# ========== LLVM ==========
# Find an llvm-config.exe on Windows. Prefer the MSYS2 MinGW LLVM (the only
# distribution that ships llvm-config with matching libs + headers). Returns
# the path with forward slashes (clean for GN args), or $null if not found.

function Find-LlvmConfig {
  $candidates = @()

  if ($env:LLVM_CONFIG) { $candidates += $env:LLVM_CONFIG }
  if ($env:MSYSTEM_PREFIX) { $candidates += (Join-Path $env:MSYSTEM_PREFIX "bin\llvm-config.exe") }

  $roots = @($env:MSYS2, "C:\msys64", "C:\clang64", $env:LLVM_INSTALL_DIR,
             "C:\Program Files\LLVM", "C:\ProgramData\chocolatey\lib\llvm\tools")
  foreach ($root in $roots) {
    if (-not $root) { continue }
    foreach ($sub in @("mingw64\bin", "clang64\bin", "ucrt64\bin", "bin")) {
      $candidates += (Join-Path $root (Join-Path $sub "llvm-config.exe"))
    }
  }

  foreach ($path in $candidates) {
    if (Test-Path $path -PathType Leaf) {
      return ($path -replace '\\', '/')
    }
  }

  $cmd = Get-Command llvm-config -ErrorAction SilentlyContinue
  if ($cmd) { return ($cmd.Source -replace '\\', '/') }
  return $null
}

# ========== main ==========

function Invoke-Setup {
  New-Item -ItemType Directory -Force -Path $BIN | Out-Null

  Info "Kinglet dev setup (Windows)"

  Install-Gn
  Install-Ninja
  Write-Env
  Add-BinToPath
  Info ""

  # LLVM (detect + report); build.ps1 reuses Find-LlvmConfig at build time.
  $llvmCfg = Find-LlvmConfig
  if ($llvmCfg) {
    $env:LLVM_CONFIG = $llvmCfg
    Info "found llvm-config: $llvmCfg"
    Info "LLVM ready - native backend will be built (scripts/build.ps1 wires it up)"
  } else {
    Warn "no LLVM found - builds will be compile-only (native backend disabled)"
    Warn "install MSYS2 MinGW LLVM (e.g. 'pacman -S mingw-w64-x86-64-llvm') or set LLVM_CONFIG"
  }

  Info ""
  Info "Done. gn/ninja are on PATH now (new shells too). Next:"
  if ($llvmCfg) {
    Info "  pwsh -File scripts/build.ps1"
  } else {
    Info "  pwsh -File scripts/build.ps1   # compile-only; install LLVM for the native backend"
  }
}

# Run main only when executed directly, not when dot-sourced (so build.ps1 can
# reuse Find-LlvmConfig / Add-BinToPath without re-running the install flow).
if ($MyInvocation.InvocationName -ne ".") {
  Invoke-Setup
}

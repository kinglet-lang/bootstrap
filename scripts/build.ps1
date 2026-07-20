# Build the kinglet compiler on Windows and wire the binary onto PATH.
#
#   pwsh -File scripts/build.ps1                  # gn gen + ninja (LLVM if found)
#   pwsh -File scripts/build.ps1 -DebugBuild      # debug build (-g, no -O2)
#   pwsh -File scripts/build.ps1 -NoLlm           # force compile-only, no LLVM
#   pwsh -File scripts/build.ps1 -Out out\Foo      # custom output dir
#   pwsh -File scripts/build.ps1 -GnArgs 'optimize="-O2"'
#
# When an MSYS2 MinGW LLVM is detected, the native LLVM backend is built
# (kinglet + kinglet_rt); the whole build is wired to that MinGW clang++
# toolchain (clang_base_path) so the LLVM libraries link correctly. Without
# LLVM, the build is compile-only. Requires scripts/setup.ps1 to have been run
# at least once (for GN + Ninja under .\tools\bin).
#
# Set BUILD_CI=1 to skip binary staging (CI/automation use).
# Set KINGLET_CXX to the MinGW clang++ the native backend should use at runtime
# when AOT-linking user programs (see docs/BUILD.md, "Building on Windows").
#
# After a successful build, kinglet.exe (and klet.exe, its alias) are staged
# into .\tools\bin and added to PATH via the same profile mechanism as setup.ps1.

[CmdletBinding()]
param(
  [switch]$DebugBuild,
  [switch]$NoLlm,
  [string]$Out = "out\Default",
  [string]$GnArgs = ""
)

$ErrorActionPreference = "Stop"

$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$ROOT       = Split-Path -Parent $SCRIPT_DIR
$TOOLS      = Join-Path $ROOT "tools"
$BIN        = Join-Path $TOOLS "bin"

function Info($msg) { Write-Host $msg -ForegroundColor DarkGray }
function Warn($msg) { Write-Host $msg -ForegroundColor Yellow }
function Fail($msg) { Write-Host $msg -ForegroundColor Red; exit 1 }

# Stage the transitive closure of DLLs that $exe needs and that live in $srcBin,
# into $dest. Walking the full import graph (not just kinglet.exe's direct
# imports) catches indirect deps such as libwinpthread, which libstdc++ pulls in.
function Get-DllImports($binary, $objdump) {
  if (-not (Test-Path $binary -PathType Leaf)) { return @() }
  & $objdump -p $binary | Select-String '^\s*DLL Name:\s*(.+)$' |
    ForEach-Object { ($_.Matches[0].Groups[1].Value).Trim() }
}

function Copy-RequiredDlls($exe, $srcBin, $dest) {
  if (-not $srcBin -or -not (Test-Path $srcBin)) { return }
  $objdump = Join-Path $srcBin "objdump.exe"
  if (-not (Test-Path $objdump)) {
    Warn "objdump not found in $srcBin; skipping DLL staging (add $srcBin to PATH at runtime)"
    return
  }

  $copied = @{}
  $queue = New-Object System.Collections.Generic.Queue[string]
  $queue.Enqueue($exe)
  while ($queue.Count -gt 0) {
    $current = $queue.Dequeue()
    foreach ($dll in (Get-DllImports $current $objdump)) {
      if ($copied.ContainsKey($dll)) { continue }
      $src = Join-Path $srcBin $dll
      if (Test-Path $src -PathType Leaf) {
        Copy-Item -Force $src (Join-Path $dest $dll)
        Info "staged $dll"
        $copied[$dll] = $true
        # Follow this DLL's own imports to catch transitive deps.
        $queue.Enqueue((Join-Path $dest $dll))
      }
    }
  }
}

Set-Location $ROOT

# Reuse setup.ps1's helpers (Find-LlvmConfig, Add-BinToPath) without running
# its GN/Ninja install flow.
. (Join-Path $SCRIPT_DIR "setup.ps1")

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

$enableLlvm = $false
$clangBase  = ""
if (-not $NoLlm) {
  $llvmCfg = Find-LlvmConfig
  if ($llvmCfg) {
    Info "found llvm-config: $llvmCfg"
    # The MinGW LLVM must be compiled and linked with its own clang++ (MSVC-ABI
    # clang cannot link these libraries), so point the win toolchain at it.
    $clangBase = Split-Path $llvmCfg -Parent
    $gnArgs = "$gnArgs enable_llvm=true llvm_config=`"$llvmCfg`" clang_base_path=`"$clangBase`""
    $enableLlvm = $true
  } else {
    Warn "no LLVM found - building without native backend (compile-only)"
    Warn "install MSYS2 MinGW LLVM or pass -NoLlm to silence this"
  }
} else {
  Info "-NoLlm: building without native backend"
}

# Append extra GN args (e.g. optimize, coverage).
if ($GnArgs) { $gnArgs = "$gnArgs $GnArgs" }

Info "gn gen $Out --args=`"$gnArgs`""
& $gn gen $Out --args="$gnArgs"
if ($LASTEXITCODE -ne 0) { Fail "gn gen failed" }

# ========== build ==========

# kinglet_rt is LLVM-independent; always build it so compile-only runs still
# type-check the runtime (catches Windows/MinGW-only regressions).
$targets = @("kinglet", "kinglet_rt")

Info "ninja -C $Out $($targets -join ' ')"
& $ninja -C $Out @targets
if ($LASTEXITCODE -ne 0) { Fail "ninja build failed" }

$builtBin = Join-Path $ROOT (Join-Path $Out "kinglet.exe")
if (-not (Test-Path $builtBin)) {
  Fail "build finished but $builtBin is missing"
}

# ========== stage kinglet/klet + wire PATH ==========

if ($env:BUILD_CI -eq "1") {
  Info "BUILD_CI=1: skipping binary staging"
  exit 0
}

New-Item -ItemType Directory -Force -Path $BIN | Out-Null
Copy-Item -Force $builtBin (Join-Path $BIN "kinglet.exe")

if ($enableLlvm) {
  # The kinglet binary resolves the runtime archive relative to its own
  # directory (resolve_rt_lib in main.cc). Stage it alongside the binary.
  $rtLib = Join-Path $ROOT (Join-Path $Out "obj\runtime\kinglet_rt.lib")
  if (Test-Path $rtLib) {
    Copy-Item -Force $rtLib (Join-Path $BIN "kinglet_rt.lib")
    Info "staged $BIN\kinglet_rt.lib"
  }
  # The MinGW build imports libLLVM-*.dll and the MinGW runtime DLLs. Stage the
  # exact set kinglet.exe imports so the binary runs without MinGW on PATH.
  Copy-RequiredDlls $builtBin $clangBase $BIN
}

$aliasScript = Join-Path $SCRIPT_DIR "stage-klet-alias.ps1"
if (Test-Path $aliasScript) {
  try {
    & pwsh -NoProfile -File $aliasScript $BIN
  } catch {
    Warn "klet alias staging failed (non-fatal): $($_.Exception.Message)"
  }
}

Add-BinToPath

Info ""
Info "Done: $BIN\kinglet.exe"
& (Join-Path $BIN "kinglet.exe") --version 2>$null
Info "Restart your shell, or it's already on PATH for this session."

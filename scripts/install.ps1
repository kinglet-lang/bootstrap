# Kinglet Windows installer — one-liner downloads the latest stable release
# and puts kinglet on PATH.
#
#   irm https://kinglet-lang.org/install.ps1 | iex
#
# With KINGLET_VERSION to install a specific or prerelease version:
#   $env:KINGLET_VERSION = "v0.1.0-rc.3"; irm ... | iex
#
# Environment overrides:
#   KINGLET_VERSION       Tag to install (e.g. v0.1.0-rc.3). Default: latest
#                         stable (non-prerelease) release.
#   KINGLET_INSTALL_DIR   Install prefix. Default: $HOME\.kinglet
#   KINGLET_REPO          GitHub owner/repo. Default: kinglet-lang/bootstrap
#   KINGLET_NO_MODIFY_PATH=1  Skip persisting PATH to the user environment.
#
# Mirrors scripts/install.sh for macOS/Linux. Windows 10 version 1803 or later
# is required (tar.exe must be available on PATH). The binary is installed
# under $KINGLET_INSTALL_DIR\bin; a klet.exe hard link is created alongside
# kinglet.exe. PATH is wired for both the current session and persistently
# (user-level, no admin needed).

$ErrorActionPreference = "Stop"

# Ensure TLS 1.2 is enabled for GitHub API requests (some older PowerShell
# defaults may lack it). OR the flag into the protocol set; never downgrade.
try {
  [Net.ServicePointManager]::SecurityProtocol =
    [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
} catch {}

$Script:Repo = if ($env:KINGLET_REPO) { $env:KINGLET_REPO } else { "kinglet-lang/bootstrap" }
$Script:InstallDir = if ($env:KINGLET_INSTALL_DIR) { $env:KINGLET_INSTALL_DIR } else { "$HOME\.kinglet" }
$Script:BinDir = Join-Path $Script:InstallDir "bin"

function Info($msg) { Write-Host ">" -ForegroundColor DarkGray -NoNewline; Write-Host " $msg" }
function Warn($msg) { Write-Host "!" -ForegroundColor Yellow -NoNewline; Write-Host " $msg" }
function Fail($msg) { Write-Host "x" -ForegroundColor Red -NoNewline; Write-Host " $msg"; exit 1 }

# ========== version resolution ==========

# Tag carries a pre-release suffix (mirrors install.sh's is_prerelease_tag).
function Test-PrereleaseTag($tag) {
  return $tag -match '-(rc|pre|alpha|beta|dev)'
}

function Resolve-Version {
  if ($env:KINGLET_VERSION) {
    Info "KINGLET_VERSION=$($env:KINGLET_VERSION) (explicit override)"
    return $env:KINGLET_VERSION
  }

  # GitHub's releases/latest is, by definition, the most recent non-prerelease
  # release — so it already excludes rc/alpha/beta tags. It 404s when no stable
  # release has been published yet.
  $api = "https://api.github.com/repos/$Script:Repo/releases/latest"
  $release = Invoke-RestMethod -Uri $api -ErrorAction SilentlyContinue
  if (-not $release -or -not $release.tag_name) {
    Warn "No stable (non-prerelease) release found on $Script:Repo yet."
    Fail "Set `$env:KINGLET_VERSION = 'v0.1.0-rc.3' to install a prerelease, or 'v0.1.1' for the latest."
  }
  $tag = $release.tag_name

  # Defence in depth: never auto-install something that looks like a prerelease.
  if (Test-PrereleaseTag $tag) {
    Fail "latest release '$tag' looks like a prerelease; refusing to auto-install. Set `$env:KINGLET_VERSION to force."
  }

  Info "latest stable release: $tag"
  return $tag
}

# ========== installation ==========

function Install-Kinglet {
  $version = $Script:Version
  $target = "kinglet-windows-x64"
  $archiveName = "$target.tar.gz"

  $baseUrl = if ($env:KINGLET_BASE_URL) {
    $env:KINGLET_BASE_URL
  } else {
    "https://github.com/$Script:Repo/releases/download/$version"
  }

  # Work in a temp directory; clean up when done.
  $tmpDir = Join-Path $env:TEMP "kinglet-setup"
  Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null

  try {
    $archive = Join-Path $tmpDir $archiveName
    Info "downloading $baseUrl/$archiveName"
    Invoke-WebRequest -Uri "$baseUrl/$archiveName" -OutFile $archive

    # Verify checksum when published; non-fatal otherwise.
    $sumsFile = Join-Path $tmpDir "SHA256SUMS"
    try {
      Invoke-WebRequest -Uri "$baseUrl/SHA256SUMS" -OutFile $sumsFile -ErrorAction Stop
      $expected = (Get-Content $sumsFile | Where-Object { $_ -match [regex]::Escape($archiveName) } |
                   ForEach-Object { ($_ -split '\s+')[0] } | Select-Object -First 1)
      if ($expected) {
        $actual = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLower()
        if ($actual -ne $expected.ToLower().Trim()) {
          Fail "checksum mismatch for $archiveName (expected $expected, got $actual)"
        }
        Info "checksum verified"
      }
    } catch {
      Warn "SHA256SUMS not published for $version; skipping verification"
    }

    Info "installing to $Script:BinDir"
    New-Item -ItemType Directory -Force -Path $Script:BinDir | Out-Null

    $kinglet = Join-Path $Script:BinDir "kinglet.exe"

    # Windows 10 1803+ ships tar.exe in System32; require it.
    if (-not (Get-Command tar.exe -ErrorAction SilentlyContinue)) {
      Fail "tar.exe not found. Windows 10 version 1803 or later is required."
    }
    tar xzf $archive -C $Script:BinDir 2>&1 | Out-Null
    if (-not (Test-Path $kinglet)) { Fail "extraction failed (kinglet.exe not found)" }

    # klet.exe as a hard link to kinglet.exe (mirrors stage-klet-alias.ps1).
    $klet = Join-Path $Script:BinDir "klet.exe"
    if (Test-Path $kinglet) {
      Remove-Item $klet -Force -ErrorAction SilentlyContinue
      try {
        New-Item -ItemType HardLink -Path $klet -Target $kinglet -Force | Out-Null
        Info "staged klet.exe"
      } catch {
        cmd /c "mklink /H `"$klet`" `"$kinglet`"" 2>$null
        if ($LASTEXITCODE -eq 0) {
          Info "staged klet.exe (mklink)"
        } else {
          Warn "klet.exe hardlink failed; use kinglet.exe directly"
        }
      }
    }

    $installed = & $kinglet --version 2>$null
    Info "Installed: $installed -> $kinglet"
  } finally {
    Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
  }
}

# ========== PATH wiring ==========
# Persist to the user-level PATH (no admin needed) + current session.

function Add-ToPath {
  if ($env:KINGLET_NO_MODIFY_PATH -eq "1") { return }

  # Current session.
  if (($env:PATH -split ';') -notcontains $Script:BinDir) {
    $env:PATH = "$Script:BinDir;$env:PATH"
  }

  # Persist for future shells (user-level environment variable, no admin).
  $userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
  if ($userPath -notmatch [regex]::Escape($Script:BinDir)) {
    [Environment]::SetEnvironmentVariable("PATH", "$Script:BinDir;$userPath", "User")
    Info "added $Script:BinDir to user PATH"
  }
}

# ========== main ==========

function Invoke-Install {
  Info "Kinglet installer (Windows)"

  $Script:Version = Resolve-Version
  Install-Kinglet
  Add-ToPath

  Info ""
  Info "Done. kinglet is on PATH for this session."
  Info "Re-open your terminal, or run: `$env:PATH = `"$Script:BinDir;`$env:PATH`""
  Info "Verify with: kinglet --version"

  # The native (LLVM) backend shells out to a C++ compiler to link programs.
  if (-not (Get-Command clang++.exe -ErrorAction SilentlyContinue)) {
    Warn "no 'clang++.exe' found — the native backend ('kinglet run', 'kinglet build') needs one to link programs."
  }
}

# Run main.
Invoke-Install

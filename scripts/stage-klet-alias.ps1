# Create klet.exe as a hard link to kinglet.exe (PowerShell / native Windows).
param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$Dist
)

$ErrorActionPreference = 'Stop'

$kinglet = Join-Path $Dist 'kinglet.exe'
$klet = Join-Path $Dist 'klet.exe'

if (-not (Test-Path $kinglet)) {
  Write-Error "missing $kinglet"
}

Remove-Item $klet, (Join-Path $Dist 'klet.cmd'), (Join-Path $Dist 'klet') -Force -ErrorAction SilentlyContinue

$mklink = cmd /c "mklink /H `"$klet`" `"$kinglet`"" 2>&1
if ($LASTEXITCODE -eq 0) {
  Write-Host 'staged klet.exe as hard link to kinglet.exe'
  exit 0
}

$fsutil = cmd /c "fsutil hardlink create `"$klet`" `"$kinglet`"" 2>&1
if ($LASTEXITCODE -eq 0) {
  Write-Host 'staged klet.exe as hard link to kinglet.exe (fsutil)'
  exit 0
}

Write-Error "hard link failed:`n$mklink`n$fsutil`nUse kinglet.exe directly."

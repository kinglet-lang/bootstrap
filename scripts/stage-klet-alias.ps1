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

try {
  New-Item -ItemType HardLink -Path $klet -Target $kinglet | Out-Null
  Write-Host 'staged klet.exe as hard link to kinglet.exe'
  exit 0
} catch {
  $mklink = cmd /c "mklink /H `"$klet`" `"$kinglet`"" 2>&1
  if ($LASTEXITCODE -eq 0) {
    Write-Host 'staged klet.exe as hard link to kinglet.exe (mklink)'
    exit 0
  }
  Write-Error "hard link failed: $($_.Exception.Message)`n$mklink`nUse kinglet.exe directly."
}

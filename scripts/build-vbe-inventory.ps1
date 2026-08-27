[CmdletBinding()]
param([string]$BuildId)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\vbe-inventory"
. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "vbe-inventory-local" }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw "Invalid BuildId" }

$watcomRoot = if ($env:WATCOM) { $env:WATCOM } else { "C:\WATCOM" }
$compiler = @((Join-Path $watcomRoot "binnt64\wcl.exe"),
              (Join-Path $watcomRoot "binnt\wcl.exe")) |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $compiler) { throw "Open Watcom DOS compiler was not found." }
$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = Join-Path $watcomRoot "h"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir "v9xvbe.exe"
Push-Location $outputDir
try {
    & $compiler "-bt=dos" "-ms" "-zq" "-wx" `
        "-i=$(Join-Path $repoRoot 'include')" `
        "-dV9X_BUILD_ID=`"$BuildId`"" "-fe=$exe" `
        (Join-Path $repoRoot "tools\diag\vbe_inventory_dos.c")
    if ($LASTEXITCODE -ne 0) { throw "Failed to build the DOS VBE inventory." }
} finally { Pop-Location }

$bytes = [IO.File]::ReadAllBytes($exe)
if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
    throw "The VBE inventory is not an MZ executable."
}
$text = [Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @($BuildId, "Velocity9x VBE inventory", "query-only")) {
    if (-not $text.Contains($marker)) { throw "VBE inventory lacks marker $marker" }
}
Write-Output "Built no-mode-change DOS VBE inventory: $exe"

[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = "C:\98DDK",
    [switch]$DisableVbeCollect
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\minivdd32"

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "minivdd-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "BuildId may contain only letters, digits, dot, underscore, plus, and hyphen."
}

$assembler = Join-Path $DdkRoot "bin\win98\ML.EXE"
$linker = Join-Path $DdkRoot "bin\LINK.EXE"
$ddkInclude = Join-Path $DdkRoot "inc\win98"
$requiredInputs = @($assembler, $linker,
                    (Join-Path $ddkInclude "VMM.INC"),
                    (Join-Path $ddkInclude "MINIVDD.INC"))
$missingInputs = @($requiredInputs | Where-Object {
    -not (Test-Path -LiteralPath $_)
})
if ($missingInputs.Count -ne 0) {
    throw "Required Windows 98 DDK inputs are missing: $($missingInputs -join ', ')"
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$buildInclude = Join-Path $outputDir "V9XBUILD.INC"
$definitionFile = Join-Path $outputDir "v9xmini.def"
$objectPath = Join-Path $outputDir "loader.obj"
$vxdPath = Join-Path $outputDir "v9xmini.vxd"
$mapPath = Join-Path $outputDir "v9xmini.map"

$buildIncludeLines = @(
    "V9xMiniVddBuildId db `"velocity9x:$BuildId`", 0",
    "V9xMiniInitLine db `"V9X-MINI init build=$BuildId`", 13, 10",
    "V9xMiniInitLineLength equ `$ - V9xMiniInitLine",
    "V9xMiniPowerCallbacksLine db `"V9X-MINI power-callbacks-ok callbacks=4 build=$BuildId`", 13, 10",
    "V9xMiniPowerCallbacksLineLength equ `$ - V9xMiniPowerCallbacksLine",
    "V9xMiniDefaultsLine db `"V9X-MINI power-defaults callbacks=0 build=$BuildId`", 13, 10",
    "V9xMiniDefaultsLineLength equ `$ - V9xMiniDefaultsLine",
    "V9xMiniPowerOnLine db `"V9X-MINI monitor-power D0`", 13, 10",
    "V9xMiniPowerOnLineLength equ `$ - V9xMiniPowerOnLine",
    "V9xMiniPowerOffLine db `"V9X-MINI monitor-power low`", 13, 10",
    "V9xMiniPowerOffLineLength equ `$ - V9xMiniPowerOffLine",
    "V9xMiniFailLine db `"V9X-MINI init-fail build=$BuildId`", 13, 10",
    "V9xMiniFailLineLength equ `$ - V9xMiniFailLine",
    "V9xMiniVbeStartLine db `"V9X-MINI vbe-collect start build=$BuildId`", 13, 10",
    "V9xMiniVbeStartLineLength equ `$ - V9xMiniVbeStartLine",
    "V9xMiniVbeDoneLine db `"V9X-MINI vbe-collect done`", 13, 10",
    "V9xMiniVbeDoneLineLength equ `$ - V9xMiniVbeDoneLine",
    "V9xMiniVbeCallLine db `"V9X-MINI vbe-call fn=`"",
    "V9xMiniVbeCallFnHex db `"0000`"",
    "db `" arg=`"",
    "V9xMiniVbeCallArgHex db `"0000`", 13, 10",
    "V9xMiniVbeCallLineLength equ `$ - V9xMiniVbeCallLine",
    "V9xMiniVbeCallRetLine db `"V9X-MINI vbe-call ret=`"",
    "V9xMiniVbeCallRetHex db `"0000`", 13, 10",
    "V9xMiniVbeCallRetLineLength equ `$ - V9xMiniVbeCallRetLine"
)
if ($DisableVbeCollect) {
    $buildIncludeLines += @(
        "V9xMiniVbeDisabledLine db `"V9X-MINI vbe-collect disabled build=$BuildId`", 13, 10",
        "V9xMiniVbeDisabledLineLength equ `$ - V9xMiniVbeDisabledLine"
    )
}
Set-Content -LiteralPath $buildInclude -Encoding Ascii -Value $buildIncludeLines
Set-Content -LiteralPath $definitionFile -Encoding Ascii -Value @(
    "VXD V9XMINI DYNAMIC",
    "DESCRIPTION 'Velocity9x S3 ViRGE mini-VDD with monitor power management'",
    "SEGMENTS",
    "    _LTEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _LDATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _TEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _DATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    CONST CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _BSS CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _ITEXT CLASS 'ICODE' DISCARDABLE",
    "    _IDATA CLASS 'ICODE' DISCARDABLE",
    "EXPORTS",
    "    V9XMINI_DDB @1"
)

$sourcePath = Join-Path $repoRoot "src\minivdd32\loader.asm"
$assemblerArguments = @(
    "-coff", "-DBLD_COFF", "-W2", "-Zd", "-c", "-Cx",
    "-DMASM6", "-Sg", "-DVGA", "-DVGA31", "-DMINIVDD=1",
    "-I$ddkInclude", "-I$outputDir", "-Fo$objectPath", $sourcePath
)
if ($DisableVbeCollect) {
    $assemblerArguments = @("-DV9X_NO_VBE_COLLECT") + $assemblerArguments
}
& $assembler @assemblerArguments
if ($LASTEXITCODE -ne 0) {
    throw "The Windows 98 DDK assembler failed to build the mini-VDD skeleton."
}

$linkerArguments = @(
    "/VXD", "/NOD", $objectPath, "/IGNORE:4078", "/IGNORE:4039",
    "/OUT:$vxdPath", "/MAP:$mapPath", "/DEF:$definitionFile"
)
& $linker @linkerArguments
if ($LASTEXITCODE -ne 0) {
    throw "The Windows 98 DDK linker failed to create the mini-VDD skeleton."
}

$bytes = [System.IO.File]::ReadAllBytes($vxdPath)
$newHeaderOffset = if ($bytes.Length -ge 64) {
    [System.BitConverter]::ToInt32($bytes, 0x3c)
} else { -1 }
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a -or
    $newHeaderOffset -lt 0 -or $newHeaderOffset + 1 -ge $bytes.Length -or
    $bytes[$newHeaderOffset] -ne 0x4c -or
    $bytes[$newHeaderOffset + 1] -ne 0x45) {
    throw "The mini-VDD output is not an MZ/LE image."
}

$imageText = [System.Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @("velocity9x:$BuildId", "V9X-MINI init",
                       "V9X-MINI power-callbacks-ok",
                       "V9X-MINI monitor-power D0", "V9XMINI_DDB")) {
    if (-not $imageText.Contains($marker)) {
        throw "The mini-VDD output is missing marker $marker."
    }
}
$disabledMarker = "V9X-MINI vbe-collect disabled"
if ($DisableVbeCollect) {
    if (-not $imageText.Contains($disabledMarker)) {
        throw "The no-collect mini-VDD is missing its disabled marker."
    }
} elseif ($imageText.Contains($disabledMarker)) {
    throw "A default mini-VDD build must not carry the vbe-collect disabled marker."
}
$sourceText = Get-Content -LiteralPath $sourcePath -Raw
if (([regex]::Matches($sourceText, '(?m)^\s*MiniVDDDispatch\s+')).Count -ne 4 -or
    $sourceText -notmatch 'MiniVDDDispatch\s+VESA_SUPPORT' -or
    $sourceText -notmatch 'MiniVDDDispatch\s+VESA_CALL_POST_PROCESSING' -or
    $sourceText -notmatch 'MiniVDDDispatch\s+SET_MONITOR_POWER_STATE' -or
    $sourceText -notmatch 'MiniVDDDispatch\s+GET_MONITOR_POWER_STATE_CAPS') {
    throw "The mini-VDD must install exactly the four audited monitor-power callbacks."
}
$mapText = Get-Content -LiteralPath $mapPath -Raw
foreach ($symbol in @("V9xMini_Serial_Write", "V9xMini_Set_Dpms",
                      "MiniVDD_VESASupport",
                      "MiniVDD_VESACallPostProcessing",
                      "MiniVDD_SetMonitorPowerState",
                      "MiniVDD_GetMonitorPowerStateCaps",
                      "MiniVDD_Dynamic_Init")) {
    if ($mapText -notmatch "(?m)^.*$([regex]::Escape($symbol)).*$") {
        throw "The mini-VDD map is missing symbol $symbol."
    }
}

Write-Output "Built boot-loadable monitor-power mini-VDD candidate: $vxdPath"

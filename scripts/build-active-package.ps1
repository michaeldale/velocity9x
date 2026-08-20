[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = "C:\98DDK",
    # -1 means "use the family's DefaultMode". Any other value indexes the
    # family's Inf.ForcedModes, so the upper bound is that array's length and
    # is checked against the manifest once it is loaded rather than restated
    # here - a literal range silently went wrong whenever a family gained or
    # lost a forced mode.
    [int]$ForceModeIndex = -1,
    # Boot tracing is on by default: the driver records the furthest lifecycle
    # stage it reached in C:\V9XBOOT.INI, which the settings page reports and
    # the install guide asks testers to send in. -BootTrace is still accepted
    # so existing invocations keep working. Use -NoBootTrace to omit it.
    [switch]$BootTrace,
    [switch]$NoBootTrace,
    # Family manifest id under packaging\families.
    [string]$Family = 's3'
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "lib\family.ps1")
. (Join-Path $PSScriptRoot "lib\inf.ps1")

$familyManifest = Import-V9xFamily -RepoRoot $repoRoot -Id $Family
if ($familyManifest.Inf.Generate -eq $false) {
    throw ("Family $Family installs by guarded file replacement and has no " +
           "INF package; build it with its own packaging script.")
}
$outputDir = Join-Path $repoRoot $familyManifest.Build.PackageOutput

# Before anything is compiled: the index has to be one this family has, and
# build-win16-ddi-skeleton.ps1 is handed it further down.
$forcedModes = @($familyManifest.Inf.ForcedModes)
if ($ForceModeIndex -lt -1 -or $ForceModeIndex -ge $forcedModes.Count) {
    throw ("-ForceModeIndex must be -1 (the family default) or an index into " +
           "family $Family's $($forcedModes.Count) forced modes " +
           "(0..$($forcedModes.Count - 1)); got $ForceModeIndex.")
}

. (Join-Path $PSScriptRoot "common.ps1")
$ProductVersion = Get-V9xProductVersion -RepoRoot $repoRoot
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "phase3-matrix-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "BuildId may contain only letters, digits, dot, underscore, plus, and hyphen."
}

$traceEnabled = -not $NoBootTrace
& (Join-Path $PSScriptRoot "build-win16-ddi-skeleton.ps1") `
    -BuildId $BuildId -DdkRoot $DdkRoot -ForceModeIndex $ForceModeIndex `
    -BootTrace:$traceEnabled -Family $Family
$miniVddVbeCollect = ($familyManifest.Build.MiniVddVbeCollect -ne $false)
& (Join-Path $PSScriptRoot "build-minivdd-skeleton.ps1") `
    -BuildId $BuildId -DdkRoot $DdkRoot `
    -DisableVbeCollect:(-not $miniVddVbeCollect)
& (Join-Path $PSScriptRoot "build-settings.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-settings-page.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-gdi-smoke.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-palette-smoke.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-mode-switch.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-power-cycle.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-ddraw-probe.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-trace-dump.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-window-list.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-ddraw-hal-dll.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-vxd-loader-probe.ps1") `
    -BuildId $BuildId -DdkRoot $DdkRoot
& (Join-Path $PSScriptRoot "build-win16-loader-probe.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-driver-stage-probe.ps1") -BuildId $BuildId

$installSource = Join-Path $repoRoot "packaging\win98se\INSTALL.TXT"
$recoverSource = Join-Path $repoRoot "packaging\win98se\RECOVER.TXT"
$firstBootSource = Join-Path $repoRoot "packaging\win98se\FIRSTBOOT.TXT"
$normalRepairSource = Join-Path $repoRoot "packaging\win98se\V9XFIX.BAT"

# The INF is now generated from the family manifest rather than rewritten out
# of packaging\win98se\velocity9x.inf, so a family can carry more than one
# chip. The checked-in INF is retired at phase 8.
$defaultMode = if ($ForceModeIndex -ge 0) {
    $forcedModes[$ForceModeIndex]
} else {
    $familyManifest.Inf.DefaultMode
}
$infLines = @(New-V9xInfText -Family $familyManifest -DefaultMode $defaultMode)
Assert-V9xInf -Lines $infLines -Family $familyManifest -DefaultMode $defaultMode
$expectedHardwareIds = @(Get-V9xFamilyHardwareIds -Family $familyManifest)

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
Remove-Item -LiteralPath (Join-Path $outputDir "V9XFIX.INF") -Force `
    -ErrorAction SilentlyContinue
Copy-Item -LiteralPath (Join-Path $repoRoot (Join-Path `
        $familyManifest.Build.SkeletonOutput "v9xdisp.drv")) `
    -Destination (Join-Path $outputDir "V9XDISP.DRV") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\minivdd32\v9xmini.vxd") `
    -Destination (Join-Path $outputDir "V9XMINI.VXD") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\settings\v9xset.exe") `
    -Destination (Join-Path $outputDir "V9XSET.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\settings-page\v9xsetp.dll") `
    -Destination (Join-Path $outputDir "V9XSETP.DLL") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\ddraw-hal\v9xhal.dll") `
    -Destination (Join-Path $outputDir "V9XHAL.DLL") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\gdi-smoke\v9xgdi.exe") `
    -Destination (Join-Path $outputDir "V9XGDI.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\palette-smoke\v9xpal.exe") `
    -Destination (Join-Path $outputDir "V9XPAL.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\mode-switch\v9xmsw.exe") `
    -Destination (Join-Path $outputDir "V9XMSW.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\power-cycle\v9xpwr.exe") `
    -Destination (Join-Path $outputDir "V9XPWR.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\ddraw-probe\v9xddp.exe") `
    -Destination (Join-Path $outputDir "V9XDDP.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\trace-dump\v9xtrace.exe") `
    -Destination (Join-Path $outputDir "V9XTRACE.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\window-list\v9xwnd.exe") `
    -Destination (Join-Path $outputDir "V9XWND.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\vxd-probe\v9xprobe.vxd") `
    -Destination (Join-Path $outputDir "V9XPROBE.VXD") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\win16-loader-probe\v9x16ld.exe") `
    -Destination (Join-Path $outputDir "V9X16LD.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\driver-stage-probe\v9xstage.exe") `
    -Destination (Join-Path $outputDir "V9XSTAGE.EXE") -Force
Set-Content -LiteralPath (Join-Path $outputDir "VELOCITY9X.INF") `
    -Value $infLines -Encoding Ascii
Copy-Item -LiteralPath $installSource `
    -Destination (Join-Path $outputDir "INSTALL.TXT") -Force
Copy-Item -LiteralPath $recoverSource `
    -Destination (Join-Path $outputDir "RECOVER.TXT") -Force
Copy-Item -LiteralPath $firstBootSource `
    -Destination (Join-Path $outputDir "FIRSTBOOT.TXT") -Force
$normalRepairLines = Get-Content -LiteralPath $normalRepairSource
Set-Content -LiteralPath (Join-Path $outputDir "V9XFIX.BAT") `
    -Value $normalRepairLines -Encoding Ascii

$manifest = @(
    "Velocity9x active display bring-up package",
    "Version: $ProductVersion",
    "Build: $BuildId",
    "Target: Windows 98SE, $($expectedHardwareIds -join ', ') only",
    "Modes: $($familyManifest.Package.ModesSummary)",
    "Forced diagnostic mode index: $ForceModeIndex (-1 means registry-selected)",
    "Boot trace: $traceEnabled (writes C:\\V9XBOOT.INI)",
    "Rendering: Windows DIB Engine plus DirectDraw HAL",
    "Mini-VDD callbacks: D0-only caps + guarded VESA DPMS + Win98 power state",
    "Settings: read-only bring-up status, report, and recovery shortcut",
    "Display Properties: read-only Velocity9x tab via V9XSETP.DLL",
    "GDI test: on-screen primitives, blits, and tolerant pixel readback",
    "Palette test: 8-bit reserved-entry animation and screen readback",
    "Mode switching: live same-depth via ReEnable; depth change needs restart",
    "DirectDraw HAL: $($familyManifest.Package.HalDescription)",
    "Mode-switch test: V9XMSW.EXE (/set:WxHxB, /cycle:N, /depth:N, /cursor)",
    "Monitor-power test: V9XPWR.EXE (D3 off, then D0 wake)",
    "DirectDraw probe: V9XDDP.EXE (flip timing and mode honesty)",
    "HAL trace: driver writes C:\\V9XTRACE.INI on faults; V9XTRACE.EXE writes live C:\\V9XSNAP.INI",
    "Window inventory: V9XWND.EXE writes GDI-free C:\\V9XWND.INI",
    "Preflight: V9XSTAGE.EXE (no mode change and no installation)",
    "Status: HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED",
    "",
    "Read FIRSTBOOT.TXT, INSTALL.TXT, and RECOVER.TXT before selecting the INF."
)
Set-Content -LiteralPath (Join-Path $outputDir "MANIFEST.TXT") `
    -Encoding Ascii -Value $manifest

$hashLines = Get-ChildItem -LiteralPath $outputDir -File |
    Where-Object { $_.Name -ne "SHA256.TXT" } |
    Sort-Object Name |
    ForEach-Object {
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
        "$hash  $($_.Name)"
    }
Set-Content -LiteralPath (Join-Path $outputDir "SHA256.TXT") `
    -Encoding Ascii -Value $hashLines

$expectedPackageFiles = @(
    "FIRSTBOOT.TXT", "INSTALL.TXT", "MANIFEST.TXT", "RECOVER.TXT", "SHA256.TXT",
    "V9X16LD.EXE", "V9XDDP.EXE", "V9XDISP.DRV", "V9XFIX.BAT", "V9XHAL.DLL",
    "V9XGDI.EXE", "V9XMSW.EXE", "V9XPAL.EXE", "V9XPWR.EXE",
    "V9XMINI.VXD", "V9XPROBE.VXD",
    "V9XSET.EXE", "V9XSETP.DLL", "V9XSTAGE.EXE", "V9XTRACE.EXE",
    "V9XWND.EXE",
    "VELOCITY9X.INF"
)
$actualPackageFiles = @(Get-ChildItem -LiteralPath $outputDir -File |
    ForEach-Object { $_.Name } | Sort-Object)
$unexpectedPackageFiles = @($actualPackageFiles |
    Where-Object { $_ -notin $expectedPackageFiles })
$missingPackageFiles = @($expectedPackageFiles |
    Where-Object { $_ -notin $actualPackageFiles })
if ($unexpectedPackageFiles.Count -ne 0 -or $missingPackageFiles.Count -ne 0) {
    throw "Active package file-set mismatch. Missing: $($missingPackageFiles -join ', '); unexpected: $($unexpectedPackageFiles -join ', ')"
}

$vmStageDir = Join-Path $repoRoot $familyManifest.Build.VmStageDirectory
New-Item -ItemType Directory -Force -Path $vmStageDir | Out-Null
Remove-Item -LiteralPath (Join-Path $vmStageDir "V9XFIX.INF") -Force `
    -ErrorAction SilentlyContinue
Get-ChildItem -LiteralPath $outputDir -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $vmStageDir -Force
}

Write-Output "Built host-audited active package: $outputDir"
Write-Output "Staged for the currently mounted folder CD: $vmStageDir"
Write-Output "Guest activation remains blocked on a cold VM disk/NVR backup."

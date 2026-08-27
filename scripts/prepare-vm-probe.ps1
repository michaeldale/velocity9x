[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = "C:\98DDK"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$vmRoot = Join-Path $repoRoot "build\vm-probe"
# The probe bundle deliberately ships non-functional V9XDISP.DRV/V9XMINI.VXD
# link artifacts. They must never sit at the folder-CD root (build\vm-probe),
# where they shadow the installable package in D:\ACTIVE under the same names.
$outputDir = Join-Path $vmRoot "PROBE"
$logDir = Join-Path $repoRoot "build\vm-logs"

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "vm-probe-local"
}

& (Join-Path $PSScriptRoot "build-win32-serial-smoke.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-dos-serial-smoke.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-vxd-loader-probe.ps1") `
    -BuildId $BuildId -DdkRoot $DdkRoot
& (Join-Path $PSScriptRoot "build-win16-loader-probe.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-driver-stage-probe.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-win16-ddi-skeleton.ps1") `
    -BuildId $BuildId -DdkRoot $DdkRoot
& (Join-Path $PSScriptRoot "build-minivdd-skeleton.ps1") `
    -BuildId $BuildId -DdkRoot $DdkRoot
& (Join-Path $PSScriptRoot "build-vga-survey.ps1") -BuildId $BuildId

New-Item -ItemType Directory -Force -Path $vmRoot,$outputDir,$logDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot "build\win32-diag\v9xser.exe") `
    -Destination (Join-Path $outputDir "V9XSER.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\dos-diag\v9xser.exe") `
    -Destination (Join-Path $outputDir "V9XDOS.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\vxd-probe\v9xvxd.exe") `
    -Destination (Join-Path $outputDir "V9XVXD.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\vxd-probe\v9xprobe.vxd") `
    -Destination (Join-Path $outputDir "V9XPROBE.VXD") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\win16-loader-probe\v9x16ld.exe") `
    -Destination (Join-Path $outputDir "V9X16LD.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\driver-stage-probe\v9xstage.exe") `
    -Destination (Join-Path $outputDir "V9XSTAGE.EXE") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\win16-ddi-s3\v9xdisp.drv") `
    -Destination (Join-Path $outputDir "V9XDISP.DRV") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\minivdd32\v9xmini.vxd") `
    -Destination (Join-Path $outputDir "V9XMINI.VXD") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "build\vga-survey\V9XSURV.EXE") `
    -Destination (Join-Path $outputDir "V9XSURV.EXE") -Force

$readme = @(
    "Velocity9x VM probe bundle",
    "Build: $BuildId",
    "",
    "This PROBE folder is NOT the installable driver package.",
    "Install only from D:\ACTIVE.",
    "",
    "SAFE ACTION: run V9XSER.EXE to send one Win32 COM1 smoke line.",
    "V9XDOS.EXE is a direct-UART fallback intended for pure DOS only.",
    "SAFE LIFECYCLE PROBE: run V9XVXD.EXE with V9XPROBE.VXD beside it.",
    "The probe loads, logs, and unloads without touching display hardware.",
    "SAFE WIN16 PROBE: run V9X16LD.EXE with V9XDISP.DRV beside it.",
    "It loads the DRV and calls only the non-activating Enable inquiry path.",
    "It verifies GDIINFO plus accepted/rejected mode validation, then unloads.",
    "CONSOLIDATED DRIVER-STAGE TEST: run V9XSTAGE.EXE once.",
    "It holds V9XPROBE.VXD loaded while V9X16LD.EXE silently loads/unloads",
    "V9XDISP.DRV, reports one PASS/FAIL result, and changes no display mode.",
    "SAFE HARDWARE SURVEY: run V9XSURV.EXE from a DOS prompt.",
    "It reads PCI config space, the video BIOS, VBE and the VGA registers and",
    "writes C:\V9XDIAG\V9XSURV.INI. It sets no video mode. Boot to MS-DOS mode for a",
    "full report; a DOS box works but cannot see real register values.",
    "Add /rom for the complete video BIOS image, /notier2 to skip the",
    "vendor-specific register probe it asks about.",
    "",
    "DO NOT INSTALL V9XDISP.DRV OR V9XMINI.VXD.",
    "They are ABI/link artifacts whose initialization deliberately fails.",
    "They are included only to prove reproducible transfer into the guest."
)
Set-Content -LiteralPath (Join-Path $outputDir "README.TXT") `
    -Value $readme -Encoding Ascii

$hashLines = Get-ChildItem -LiteralPath $outputDir -File |
    Where-Object { $_.Name -ne "SHA256.TXT" } |
    Sort-Object Name |
    ForEach-Object {
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
        "$hash  $($_.Name)"
    }
Set-Content -LiteralPath (Join-Path $outputDir "SHA256.TXT") `
    -Value $hashLines -Encoding Ascii

# Older versions staged this deliberately noninstallable bundle at the mounted
# folder-CD root. Remove only that exact generated file set after the new PROBE
# bundle has been built successfully. ACTIVE and lab diagnostic files are not
# touched.
$legacyRootFiles = @(
    "SHA256.TXT", "V9X16LD.EXE", "V9XDISP.DRV", "V9XDOS.EXE",
    "V9XMINI.VXD", "V9XPROBE.VXD", "V9XSER.EXE", "V9XSTAGE.EXE",
    "V9XVXD.EXE"
)
$vmRootFull = [IO.Path]::GetFullPath($vmRoot).TrimEnd('\')
foreach ($name in $legacyRootFiles) {
    $legacyPath = [IO.Path]::GetFullPath((Join-Path $vmRoot $name))
    if ([IO.Path]::GetDirectoryName($legacyPath).TrimEnd('\') -ne $vmRootFull) {
        throw "Refusing to remove legacy probe path outside $vmRootFull."
    }
    if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
        Remove-Item -LiteralPath $legacyPath -Force
    }
}

$rootIndex = @(
    "Velocity9x VM transfer root",
    "",
    "INSTALLABLE DRIVER PACKAGE: D:\ACTIVE",
    "NONINSTALLABLE TEST/PROBE BUNDLE: D:\PROBE",
    "",
    "Never install V9XDISP.DRV or V9XMINI.VXD from D:\PROBE.",
    "Read D:\ACTIVE\FIRSTBOOT.TXT before installing the active package."
)
$rootIndexPath = Join-Path $vmRoot "START-HERE.TXT"
try {
    Set-Content -LiteralPath $rootIndexPath -Value $rootIndex -Encoding Ascii
} catch [System.IO.IOException] {
    Write-Warning "Could not refresh $rootIndexPath while the folder CD is in use. Eject it and rerun this script."
}

# README.TXT was the old root-level probe-bundle readme. It is harmless after
# the executable cleanup above, but remove it when 86Box is not holding it open
# so its obsolete paths do not compete with START-HERE.TXT.
$legacyReadmePath = Join-Path $vmRoot "README.TXT"
if (Test-Path -LiteralPath $legacyReadmePath -PathType Leaf) {
    try {
        Remove-Item -LiteralPath $legacyReadmePath -Force
    } catch [System.IO.IOException] {
        Write-Warning "Could not remove obsolete $legacyReadmePath while the folder CD is in use."
    }
}

$remainingLegacyFiles = @($legacyRootFiles | Where-Object {
    Test-Path -LiteralPath (Join-Path $vmRoot $_) -PathType Leaf
})
if ($remainingLegacyFiles.Count -ne 0) {
    throw "Legacy probe files remain at the folder-CD root: $($remainingLegacyFiles -join ', ')"
}

Write-Output "Prepared VM probe folder: $outputDir"
Write-Output "Folder-CD safety index: $rootIndexPath"
Write-Output "Configure COM1 output file: $(Join-Path $logDir 'com1.log')"
Write-Output "For live capture, configure COM1 as named-pipe server velocity9x-com1."

# Phase 0 evidence capture for the intel-gma family - the Windows half.
#
# Runs ON the target machine (the HP Mini 110-1000 netbook, Windows 10 32-bit),
# not in this repo's build environment: copy it over (ScreenConnect / USB) and
# run it in any PowerShell, or queue the same reads as a Bluetrait RMM
# execute_powershell_script task - task 303 on the Dalegroup portal is exactly
# that, and its 2026-08-17 run is recorded in
# docs\decisions\2026-08-17-intel-gma-phase0-windows-evidence.md.
# Query-only - it reads the registry and WMI and changes nothing.
#
# What it captures, and why the family needs it:
#  - PCI hardware ids incl. subsystem/revision for the IGD and its second
#    display function: the exact ids the INF claims and the audit records.
#  - The panel EDID from the monitor device: native 1024x576 timings for the
#    later native-mode phase (VBE/DDC from DOS may not reach the LVDS panel).
#  - Memory resources assigned to the IGD: the BAR layout cross-check for the
#    DOS survey's PCI dump.
#  - Win32_VideoController: the WDDM driver's view, for the record.
#
# The DOS half (tools\diag\intel_survey_dos.c, built by
# scripts\build-intel-survey.ps1) captures what only real DOS can: the VBE
# mode list, GGC/BSM, and the VBIOS image.

[CmdletBinding()]
param([string]$OutputPath)

$ErrorActionPreference = "Continue"
if (-not $OutputPath) {
    $OutputPath = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) `
        ("v9x-intel-evidence-{0}.txt" -f (Get-Date -Format "yyyyMMdd-HHmmss"))
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("Velocity9x intel-gma Phase 0 evidence (Windows half)")
$lines.Add("Captured=" + (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
$lines.Add("Computer=$env:COMPUTERNAME")
$lines.Add("Access=query-only")
$lines.Add("")

$lines.Add("== PCI display devices (Enum\PCI) ==")
$pciRoot = "HKLM:\SYSTEM\CurrentControlSet\Enum\PCI"
Get-ChildItem $pciRoot | Where-Object { $_.PSChildName -like "VEN_8086*" } | ForEach-Object {
    $deviceKey = $_
    Get-ChildItem $deviceKey.PSPath | ForEach-Object {
        $instance = Get-ItemProperty $_.PSPath
        if ($instance.Class -eq "Display" -or
            ($instance.HardwareID -join ";") -match "CC_0380|CC_0300") {
            $lines.Add("Instance=" + $deviceKey.PSChildName + "\" + $_.PSChildName)
            $lines.Add("  DeviceDesc=" + $instance.DeviceDesc)
            $lines.Add("  Class=" + $instance.Class)
            foreach ($hwid in $instance.HardwareID) { $lines.Add("  HardwareID=" + $hwid) }
            foreach ($cid in $instance.CompatibleIDs) { $lines.Add("  CompatibleID=" + $cid) }
        }
    }
}
$lines.Add("")

$lines.Add("== IGD memory resources (BAR cross-check) ==")
try {
    $gpuPnp = Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.PNPDeviceID -like "PCI\VEN_8086*" -and $_.PNPClass -eq "Display" }
    foreach ($gpu in $gpuPnp) {
        $lines.Add("Device=" + $gpu.PNPDeviceID)
        Get-CimInstance Win32_PnPAllocatedResource | Where-Object {
            $_.Dependent.DeviceID -eq $gpu.PNPDeviceID
        } | ForEach-Object {
            $lines.Add("  Resource=" + $_.Antecedent.ToString())
        }
    }
} catch { $lines.Add("MemoryResources=failed: " + $_.Exception.Message) }
$lines.Add("")

$lines.Add("== Monitor EDIDs ==")
$displayRoot = "HKLM:\SYSTEM\CurrentControlSet\Enum\DISPLAY"
if (Test-Path $displayRoot) {
    Get-ChildItem $displayRoot | ForEach-Object {
        Get-ChildItem $_.PSPath | ForEach-Object {
            $paramsPath = Join-Path $_.PSPath "Device Parameters"
            if (Test-Path $paramsPath) {
                $edid = (Get-ItemProperty $paramsPath -ErrorAction SilentlyContinue).EDID
                if ($edid) {
                    $lines.Add("Monitor=" + $_.PSPath.Substring($_.PSPath.IndexOf("DISPLAY")))
                    $lines.Add("  EDID=" + (($edid | ForEach-Object { $_.ToString("X2") }) -join ""))
                }
            }
        }
    }
}
$lines.Add("")

$lines.Add("== Win32_VideoController ==")
try {
    Get-CimInstance Win32_VideoController | ForEach-Object {
        $lines.Add("Name=" + $_.Name)
        $lines.Add("  PNPDeviceID=" + $_.PNPDeviceID)
        $lines.Add("  AdapterRAM=" + $_.AdapterRAM)
        $lines.Add("  DriverVersion=" + $_.DriverVersion)
        $lines.Add("  VideoModeDescription=" + $_.VideoModeDescription)
    }
} catch { $lines.Add("VideoController=failed: " + $_.Exception.Message) }

$lines | Out-File -FilePath $OutputPath -Encoding utf8
Write-Output "Wrote $OutputPath"

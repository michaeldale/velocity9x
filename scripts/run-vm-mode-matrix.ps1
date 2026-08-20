[CmdletBinding()]
param(
    # Path to the remote-agent controller (v9xctl.ps1). Set V9X_AGENT_CTL to
    # avoid passing it on every call; the agent lives outside this repository.
    [string]$ControllerPath = $env:V9X_AGENT_CTL,
    # Family manifest id. Supplies the guest port, the package to verify
    # against, and the mode list, so the matrix can address more than the one
    # guest the controller defaults to.
    [string]$Family = "s3",
    # Which chip of the family to run against. A multi-chip family declares one
    # VM target per chip, and the family is only proven when every one of them
    # passes from the same binary.
    [string]$ChipId,
    # Overrides the resolved guest port.
    [ValidateRange(0, 65535)]
    [int]$Port = 0,
    [string]$PackagePath,
    [string]$GuestJob = "C:\V9XREMOTE\JOBS\velocity9x-mode-matrix",
    [string]$ResultsDirectory,
    # Defaults to the family's Vm.Modes and is validated against it below.
    # Deliberately not a ValidateSet: the families no longer offer the same
    # modes, and a literal list here went stale the moment one gained a depth.
    [string[]]$Mode,
    [ValidateRange(30, 600)]
    [int]$BootTimeoutSeconds = 180,
    [ValidateRange(1, 10)]
    [int]$Repeat = 1,
    [switch]$Json
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "lib\family.ps1")

$familyManifest = Import-V9xFamily -RepoRoot $repoRoot -Id $Family
if ($familyManifest.Vm.Emulator -eq 'none') {
    throw ("Family $Family declares no emulator: it is validated on physical " +
           "hardware only. There is no VM to run the mode matrix against.")
}
$vmTarget = Get-V9xFamilyVmTarget -Family $familyManifest -ChipId $ChipId
# A family can be part emulated and part physical - the ati family pairs a
# Mach64 VT2 that 86Box emulates with a Rage Mobility that nothing does. Refuse
# the physical chip here with the same message the whole-family check gives,
# rather than falling through to a port of 0 and failing three retries later on
# a parameter-validation error that says nothing about why.
if ($vmTarget.Emulator -eq 'none') {
    throw ("Family $Family chip '$($vmTarget.ChipId)' declares no emulator: it " +
           "is validated on physical hardware only. There is no VM to run the " +
           "mode matrix against.")
}
if ($Port -eq 0) {
    $Port = $vmTarget.Port
}
$declaredModes = @($familyManifest.Vm.Modes)
if ($Mode) {
    $unknown = @($Mode | Where-Object { $declaredModes -notcontains $_ })
    if ($unknown.Count -ne 0) {
        throw ("Family $Family declares no mode " + ($unknown -join ', ') +
               "; it offers: " + ($declaredModes -join ', ') + ".")
    }
} else {
    $Mode = $declaredModes
}
if (-not $PackagePath) {
    $PackagePath = Join-Path $repoRoot ("build\{0}" -f
        (Split-Path -Leaf $familyManifest.Build.PackageOutput))
}
if (-not $ResultsDirectory) {
    $ResultsDirectory = Join-Path $repoRoot (
        "build\driver-results\mode-matrix-{0}-{1}-{2}" -f $Family,
        $vmTarget.ChipId, (Get-Date -Format "yyyyMMdd-HHmmss"))
}
if (-not $ControllerPath) {
    throw "Specify -ControllerPath (or set V9X_AGENT_CTL) to the remote agent's v9xctl.ps1."
}
foreach ($path in @($ControllerPath, $PackagePath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required path does not exist: $path"
    }
}
foreach ($file in @("V9XDISP.DRV", "V9XMINI.VXD", "V9XGDI.EXE",
                    "V9XPAL.EXE")) {
    if (-not (Test-Path -LiteralPath (Join-Path $PackagePath $file))) {
        throw "Mode-matrix package is missing $file."
    }
}

$results = [IO.Path]::GetFullPath($ResultsDirectory)
New-Item -ItemType Directory -Force -Path $results | Out-Null
$powershell = Join-Path $PSHOME "powershell.exe"

function Invoke-V9xCtlJson {
    param([string]$Operation, [string[]]$OperationArguments = @())
    $arguments = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
        $ControllerPath, $Operation, "-Json", "-Port", [string]$Port
    ) + $OperationArguments
    $lastFailure = ""
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        try {
            $lines = @(& $powershell @arguments 2>&1)
            $nativeExit = $LASTEXITCODE
        } catch {
            $lines = @($_.Exception.Message)
            $nativeExit = 1
        }
        $jsonLine = $lines | Where-Object {
            $_ -is [string] -and $_.TrimStart().StartsWith("{")
        } | Select-Object -Last 1
        if ($nativeExit -eq 0 -and $jsonLine) {
            return $jsonLine | ConvertFrom-Json
        }
        $lastFailure = $lines -join [Environment]::NewLine
        if ($attempt -lt 3) {
            Start-Sleep -Seconds 1
        }
    }
    throw "v9xctl $Operation failed after 3 attempts: $lastFailure"
}

function Invoke-GuestShell {
    param([string]$Command)
    Invoke-V9xCtlJson shell @("-ShellCommand", $Command.Replace('"', '\"'))
}

# Which Services\Class\Display\NNNN key this driver is installed under.
#
# It is not always 0001. Each guest carries a key per display driver it has
# ever hosted, so the index depends on that guest's history - the ViRGE guest's
# is 0001 and the Trio64 guest's is 0002. Writing the mode to a hardcoded 0001
# landed on whatever other driver happened to own that key, and the matrix
# still passed only because the Config\0001 half of the same .reg is what
# actually takes effect. That is a pass by accident, so the key is resolved
# from the registry instead.
function Get-V9xDisplayKeyIndex {
    $guestExport = "$GuestJob\DISPLAY.REG"
    $null = Invoke-GuestShell (
        "REGEDIT /E $guestExport " +
        "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\Display")
    $local = Join-Path $results "display-class.reg"
    $null = Invoke-V9xCtlJson get @("-Source", $guestExport, "-Destination", $local)
    $text = Get-Content -LiteralPath $local -Raw

    $pattern = '(?ms)^\[HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services' +
               '\\Class\\Display\\(\d{4})\\DEFAULT\](.*?)(?=^\[|\z)'
    foreach ($match in [regex]::Matches($text, $pattern)) {
        if ($match.Groups[2].Value -match '(?im)^"drv"\s*=\s*"v9xdisp\.drv"\s*$') {
            return $match.Groups[1].Value
        }
    }
    throw ("No Services\Class\Display key on the guest names v9xdisp.drv, so " +
           "the Velocity9x driver is not the installed display driver.")
}

function New-ModeRegistryFile {
    param([string]$Name, [int]$Width, [int]$Height, [int]$BitsPerPixel,
          [string]$DisplayKey)
    $path = Join-Path $results ("mode-{0}.reg" -f $Name)
    $lines = @(
        "REGEDIT4", "",
        ("[HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\Display\{0}\DEFAULT]" -f $DisplayKey),
        ('"Mode"="{0},{1},{2}"' -f $BitsPerPixel, $Width, $Height), "",
        "[HKEY_LOCAL_MACHINE\Config\0001\Display\Settings]",
        '"UpgradeToDefaultMode"=-',
        ('"BitsPerPixel"="{0}"' -f $BitsPerPixel),
        ('"Resolution"="{0},{1}"' -f $Width, $Height),
        '"RefreshRate"="0"'
    )
    [IO.File]::WriteAllLines($path, $lines, [Text.Encoding]::ASCII)
    $path
}

$upload = Invoke-V9xCtlJson push-tree @(
    "-Source", [IO.Path]::GetFullPath($PackagePath),
    "-Destination", $GuestJob
)
foreach ($driverFile in @("V9XDISP.DRV", "V9XMINI.VXD")) {
    $compare = Invoke-GuestShell (
        "FC /B C:\WINDOWS\SYSTEM\$driverFile $GuestJob\$driverFile")
    if ($compare.Stdout -notmatch "no differences encountered") {
        throw "Installed $driverFile does not match the package; activate it before running the matrix."
    }
}
# Resolved once, after the DRV check has confirmed this driver is the installed
# one, and reused for every mode in the run.
$displayKey = Get-V9xDisplayKeyIndex

$matrix = @()
for ($pass = 1; $pass -le $Repeat; ++$pass) {
  foreach ($name in $Mode) {
    if ($name -notmatch '^(\d+)x(\d+)x(\d+)$') {
        throw "Invalid mode name: $name"
    }
    $width = [int]$Matches[1]
    $height = [int]$Matches[2]
    $bits = [int]$Matches[3]
    $modeResults = Join-Path (Join-Path $results "pass-$pass") $name
    New-Item -ItemType Directory -Force -Path $modeResults | Out-Null
    $regFile = New-ModeRegistryFile $name $width $height $bits $displayKey
    $null = Invoke-V9xCtlJson put @(
        "-Source", $regFile, "-Destination", "$GuestJob\MODE.REG")
    $null = Invoke-GuestShell "DEL C:\V9XBOOT.INI"
    $null = Invoke-GuestShell "DEL C:\V9XGDI.INI"
    $null = Invoke-GuestShell "DEL C:\V9XPAL.INI"
    $null = Invoke-GuestShell "REGEDIT /S $GuestJob\MODE.REG"

    $reboot = Invoke-V9xCtlJson reboot @(
        "-JobId", "matrix-$pass-$name",
        "-WaitSeconds", [string]$BootTimeoutSeconds)
    $desktop = Invoke-V9xCtlJson wait-desktop @(
        "-WaitSeconds", [string]$BootTimeoutSeconds)
    $info = Invoke-V9xCtlJson info
    if ($info.ScreenWidth -ne $width -or $info.ScreenHeight -ne $height) {
        throw ("Mode {0} fell back to {1}x{2}." -f $name,
               $info.ScreenWidth, $info.ScreenHeight)
    }
    # Remote agent 0.5.2 reports BitsPerPixel 0 against this driver while
    # returning the correct value against the stock S3 driver, so it is not a
    # trustworthy depth check. Depth is verified below from V9XGDI.INI, which
    # the guest-side test reads with GetDeviceCaps and which has always
    # reported the true depth. A non-zero disagreement is still a failure.
    if ($info.BitsPerPixel -ne 0 -and $info.BitsPerPixel -ne $bits) {
        throw ("Mode {0} fell back to {1}x{2}x{3}." -f $name,
               $info.ScreenWidth, $info.ScreenHeight, $info.BitsPerPixel)
    }
    $trace = Invoke-GuestShell "TYPE C:\V9XBOOT.INI"
    if ($trace.Stdout -notmatch '(?m)^Stage=enable-ok\s*$') {
        throw "Mode $name did not reach the enable-ok driver trace."
    }

    $null = Invoke-GuestShell "START $GuestJob\V9XGDI.EXE /auto"
    $gdi = $null
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        Start-Sleep -Milliseconds 500
        $candidate = Invoke-GuestShell (
            "IF EXIST C:\V9XGDI.INI TYPE C:\V9XGDI.INI")
        if ($candidate.Stdout -match '(?m)^Result=(PASS|FAIL)\s*$') {
            $gdi = $candidate
            break
        }
    }
    if (-not $gdi -or $gdi.Stdout -notmatch '(?m)^Result=PASS\s*$') {
        throw "Mode $name failed or timed out in the GDI framebuffer test."
    }
    foreach ($expected in @(
        "Width=$width", "Height=$height", "BitsPerPixel=$bits")) {
        if ($gdi.Stdout -notmatch "(?m)^$([regex]::Escape($expected))\s*$") {
            throw "Mode $name GDI result is missing $expected."
        }
    }
    $paletteResult = "N/A"
    if ($bits -eq 8) {
        $null = Invoke-GuestShell "START $GuestJob\V9XPAL.EXE /auto"
        $palette = $null
        for ($attempt = 0; $attempt -lt 20; ++$attempt) {
            Start-Sleep -Milliseconds 500
            $candidate = Invoke-GuestShell (
                "IF EXIST C:\V9XPAL.INI TYPE C:\V9XPAL.INI")
            if ($candidate.Stdout -match '(?m)^Result=(PASS|FAIL|SKIP)\s*$') {
                $palette = $candidate
                break
            }
        }
        if (-not $palette -or
            $palette.Stdout -notmatch '(?m)^Result=PASS\s*$') {
            throw "Mode $name failed or timed out in the palette test."
        }
        foreach ($expected in @(
            "Width=$width", "Height=$height", "BitsPerPixel=8")) {
            if ($palette.Stdout -notmatch
                "(?m)^$([regex]::Escape($expected))\s*$") {
                throw "Mode $name palette result is missing $expected."
            }
        }
        $paletteResult = "PASS"
    }
    $screenshot = Invoke-V9xCtlJson screenshot @(
        "-Destination", (Join-Path $modeResults "desktop.bmp"))
    $matrix += [pscustomobject]@{
        Pass = $pass
        Mode = $name
        BootCounter = $info.BootCounter
        DriverStage = "enable-ok"
        GdiResult = "PASS"
        PaletteResult = $paletteResult
        Screenshot = $screenshot.Destination
    }
  }
}

$summary = [pscustomobject]@{
    Success = $true
    Family = $Family
    ChipId = $vmTarget.ChipId
    Profile = $vmTarget.Profile
    Port = $Port
    DisplayKey = $displayKey
    PackagePath = [IO.Path]::GetFullPath($PackagePath)
    GuestJob = $GuestJob
    Repeat = $Repeat
    ResultsDirectory = $results
    Upload = $upload
    Matrix = $matrix
}
$summary | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $results "matrix.json") -Encoding UTF8
if ($Json) {
    $summary | ConvertTo-Json -Depth 6 -Compress
} else {
    $matrix | Format-Table -AutoSize
    Write-Output "Mode matrix passed. Results: $results"
}

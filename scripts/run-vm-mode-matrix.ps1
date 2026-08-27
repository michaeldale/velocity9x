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
    # The GDI acceleration phase (docs\plans\gdi-accel-000-and-harness.md
    # Block 2) runs on every mode by default. It is a separate V9XGDI phase
    # writing a separate result file, so the existing Result=PASS keeps exactly
    # the meaning this script already relies on. Skip it only to reproduce a
    # pre-000 run.
    [switch]$SkipAccel,
    # Apply each mode with a live mode switch instead of a registry write plus a
    # reboot.
    #
    # This exists because the vbe QEMU guest cannot be rebooted reliably - its
    # reset path wedges the emulated machine, in the BIOS or in early real-mode
    # boot depending on the attempt, and only a fresh QEMU process recovers
    # (docs\issues6-08-27-qemu-vbe-guest-hangs-in-seabios-on-reset.md).
    # Live switching works fine on that guest, so this is the way to get mode
    # coverage there at all.
    #
    # It is NOT equivalent to the default path and must not be read as such. The
    # reboot path additionally exercises establishing the mode at boot: the
    # registry is what the driver reads on Enable during Windows startup, and a
    # mode that works when switched into can still fail to come up. Every result
    # records which path applied it, so the two are never silently mixed.
    [switch]$LiveSwitch,
    # QEMU monitor port, used for the scanout check below. Only reachable on QEMU
    # targets; 86Box has no equivalent and the check is skipped there.
    [int]$MonitorPort = 55559,
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

# ----------------------------------------------------------------------------
# Scanout check
#
# Every other display check in this script reads back through GDI - the GDI
# framebuffer test, the acceleration comparison, the palette test, the captured
# screenshot. All of them therefore see GDI's own writes, and a fault that
# corrupts only what the CRTC scans out is invisible to the entire matrix.
#
# That is not hypothetical. On 2026-08-27 a vbe live-switch run reported 6/6 PASS
# while the emulated scanout was showing whole-screen 2-colour vertical stripes
# and an agent screenshot of the same moment showed a clean 39-colour desktop
# (docs\issues\2026-08-27-vbe-1024x768x16-scanout-stripes.md).
#
# The QEMU monitor's screendump reads the emulated framebuffer with no driver, no
# GDI and no host renderer in between, which makes it the only instrument here
# that can see such a fault. It exists on QEMU targets only.
# ----------------------------------------------------------------------------

function Invoke-V9xQemuMonitor {
    param([string]$Command, [int]$MonitorPort, [int]$SettleMs = 3500)
    $client = $null
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $connect = $client.BeginConnect('127.0.0.1', $MonitorPort, $null, $null)
        if (-not $connect.AsyncWaitHandle.WaitOne(2000)) { return $false }
        $client.EndConnect($connect)
        $stream = $client.GetStream()
        $writer = New-Object System.IO.StreamWriter($stream)
        $writer.NewLine = "`n"
        Start-Sleep -Milliseconds 400
        if ($stream.DataAvailable) {
            $drain = New-Object byte[] 8192
            $null = $stream.Read($drain, 0, 8192)
        }
        $writer.WriteLine($Command)
        $writer.Flush()
        Start-Sleep -Milliseconds $SettleMs
        return $true
    } catch {
        return $false
    } finally {
        if ($client) { $client.Close() }
    }
}

function Measure-V9xPpmColors {
    param([string]$Path)
    # Minimal P6 reader. Counts distinct sampled colours, which is all this needs:
    # a desktop samples in the tens, and the fault this guards against samples 2.
    $bytes = [IO.File]::ReadAllBytes($Path)
    $fields = @()
    $offset = 0
    while ($fields.Count -lt 4 -and $offset -lt $bytes.Length) {
        # Skip whitespace, then read one token.
        while ($offset -lt $bytes.Length -and
               [char]$bytes[$offset] -match '\s') { ++$offset }
        if ([char]$bytes[$offset] -eq '#') {
            while ($offset -lt $bytes.Length -and $bytes[$offset] -ne 10) { ++$offset }
            continue
        }
        $token = ''
        while ($offset -lt $bytes.Length -and
               [char]$bytes[$offset] -notmatch '\s') {
            $token += [char]$bytes[$offset]; ++$offset
        }
        $fields += $token
    }
    ++$offset
    if ($fields[0] -ne 'P6') { throw "Not a P6 PPM: $Path" }
    $width = [int]$fields[1]
    $height = [int]$fields[2]
    $seen = New-Object 'System.Collections.Generic.HashSet[int]'
    for ($y = 0; $y -lt $height; $y += 37) {
        for ($x = 0; $x -lt $width; $x += 29) {
            $i = $offset + (($y * $width) + $x) * 3
            if (($i + 2) -lt $bytes.Length) {
                $null = $seen.Add(($bytes[$i] -shl 16) -bor
                                  ($bytes[$i + 1] -shl 8) -bor $bytes[$i + 2])
            }
        }
    }
    return [pscustomobject]@{
        Width = $width; Height = $height; DistinctColors = $seen.Count
    }
}

# The observed fault samples 2 distinct colours; a Windows desktop samples 26-40.
# Six sits clear of both, and tolerates an unusually plain screen rather than
# failing a healthy run.
$script:V9xScanoutMinColors = 6

# Resolved once, the same way run-family-enable-gate.ps1 does it: a per-target
# Emulator overrides, and the family's Vm.Emulator is the default. Reading only
# the per-target field skipped the scanout check on every mode of a QEMU guest.
$script:V9xEmulator = if ($vmTarget.Emulator) { $vmTarget.Emulator }
                      else { $familyManifest.Vm.Emulator }

  foreach ($name in $Mode) {
    if ($name -notmatch '^(\d+)x(\d+)x(\d+)$') {
        throw "Invalid mode name: $name"
    }
    $width = [int]$Matches[1]
    $height = [int]$Matches[2]
    $bits = [int]$Matches[3]
    $modeResults = Join-Path (Join-Path $results "pass-$pass") $name
    New-Item -ItemType Directory -Force -Path $modeResults | Out-Null
    if ($LiveSwitch) {
        # A switch into the mode the guest is *already* in is a no-op: the driver
        # gets no Disable/Enable cycle, so it never rewrites V9XBOOT.INI, and the
        # enable-ok check below then fails against the file this block just
        # deleted. That is not hypothetical - it is what happened the first time
        # a run began with the guest already sitting in mode 1, left there by an
        # earlier aborted run.
        #
        # So force a real transition first, via any other declared mode. Using a
        # declared mode means the intermediate is one this family is known to
        # support, rather than something invented here.
        $current = Invoke-V9xCtlJson info
        if ($current.ScreenWidth -eq $width -and $current.ScreenHeight -eq $height) {
            $stepAside = @($Mode | Where-Object { $_ -ne $name })[0]
            if ($stepAside) {
                $null = Invoke-V9xCtlJson exec @(
                    "-Application", "$GuestJob\V9XMSW.EXE",
                    "-Arguments", "/set:$stepAside",
                    "-WorkingDirectory", $GuestJob,
                    "-TimeoutSeconds", "180")
                Start-Sleep -Seconds 2
            }
        }
        # The driver rewrites V9XBOOT.INI on every Enable, so deleting it first
        # keeps the enable-ok check below about *this* mode rather than the one
        # the guest happened to boot in. Deleted after the step-aside above, so
        # that switch's trace does not satisfy the check either.
        $null = Invoke-GuestShell "DEL C:\V9XDIAG\V9XBOOT.INI"
        $null = Invoke-GuestShell "DEL C:\V9XDIAG\V9XGDI.INI"
        $null = Invoke-GuestShell "DEL C:\V9XDIAG\V9XPAL.INI"
        $switch = Invoke-V9xCtlJson exec @(
            "-Application", "$GuestJob\V9XMSW.EXE",
            "-Arguments", "/set:$name",
            "-WorkingDirectory", $GuestJob,
            "-TimeoutSeconds", "180")
        if ($switch.ExitCode -ne 0) {
            throw ("Mode {0} live switch exited {1}." -f $name, $switch.ExitCode)
        }
        # V9XMSW writes its own verdict; a non-zero exit is not the only way for
        # a switch to fail.
        $mswReport = Invoke-GuestShell "TYPE C:\V9XDIAG\V9XMSW.INI"
        if ($mswReport.Stdout -notmatch '(?m)^Result=PASS\s*$') {
            throw ("Mode {0} live switch did not report Result=PASS." -f $name)
        }
        # The agent's cached screen metrics can lag a live switch, so poll rather
        # than trust the first read.
        $info = $null
        for ($settle = 0; $settle -lt 20; ++$settle) {
            Start-Sleep -Milliseconds 500
            $info = Invoke-V9xCtlJson info
            if ($info.ScreenWidth -eq $width -and $info.ScreenHeight -eq $height) {
                break
            }
        }
        $appliedBy = "live-switch"
    } else {
        $regFile = New-ModeRegistryFile $name $width $height $bits $displayKey
        $null = Invoke-V9xCtlJson put @(
            "-Source", $regFile, "-Destination", "$GuestJob\MODE.REG")
        $null = Invoke-GuestShell "DEL C:\V9XDIAG\V9XBOOT.INI"
        $null = Invoke-GuestShell "DEL C:\V9XDIAG\V9XGDI.INI"
        $null = Invoke-GuestShell "DEL C:\V9XDIAG\V9XPAL.INI"
        $null = Invoke-GuestShell "REGEDIT /S $GuestJob\MODE.REG"

        $reboot = Invoke-V9xCtlJson reboot @(
            "-JobId", "matrix-$pass-$name",
            "-WaitSeconds", [string]$BootTimeoutSeconds)
        $desktop = Invoke-V9xCtlJson wait-desktop @(
            "-WaitSeconds", [string]$BootTimeoutSeconds)
        $info = Invoke-V9xCtlJson info
        $appliedBy = "reboot"
    }
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
    $trace = Invoke-GuestShell "TYPE C:\V9XDIAG\V9XBOOT.INI"
    if ($trace.Stdout -notmatch '(?m)^Stage=enable-ok\s*$') {
        throw "Mode $name did not reach the enable-ok driver trace."
    }

    $null = Invoke-GuestShell "START $GuestJob\V9XGDI.EXE /auto"
    $gdi = $null
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        Start-Sleep -Milliseconds 500
        $candidate = Invoke-GuestShell (
            "IF EXIST C:\V9XDIAG\V9XGDI.INI TYPE C:\V9XDIAG\V9XGDI.INI")
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
    # The phase that can fail on content rather than on liveness: a seeded
    # stream of fills and screen-to-screen copies mirrored into a reference DC,
    # compared periodically, and then checked against the driver's own
    # counters. Its most important assertion is the one that fails when a
    # primitive is advertised and enabled and its counter is nonetheless zero -
    # without which the comparison could pass while every operation quietly
    # took the decline path.
    #
    # Given a much longer poll budget than the phases above deliberately: five
    # hundred drawing operations and twenty full-region readbacks on an
    # emulated Pentium are seconds, not milliseconds.
    $accelResult = "SKIP"
    if (-not $SkipAccel) {
        $null = Invoke-GuestShell "DEL C:\V9XDIAG\V9XACCE.INI"
        $null = Invoke-GuestShell "START $GuestJob\V9XGDI.EXE /accel"
        $accel = $null
        for ($attempt = 0; $attempt -lt 240; ++$attempt) {
            Start-Sleep -Milliseconds 500
            $candidate = Invoke-GuestShell (
                "IF EXIST C:\V9XDIAG\V9XACCE.INI TYPE C:\V9XDIAG\V9XACCE.INI")
            if ($candidate.Stdout -match '(?m)^Result=(PASS|FAIL)\s*$') {
                $accel = $candidate
                break
            }
        }
        if (-not $accel) {
            throw "Mode $name timed out in the GDI acceleration phase."
        }
        Set-Content -LiteralPath (Join-Path $modeResults "V9XACCE.INI") `
            -Value $accel.Stdout -Encoding Ascii
        if ($accel.Stdout -notmatch '(?m)^Result=PASS\s*$') {
            throw ("Mode $name failed the GDI acceleration phase. " +
                   "C:\V9XDIAG\V9XACCE.INI said:" + [Environment]::NewLine +
                   $accel.Stdout)
        }
        $accelResult = "PASS"
    }
    $paletteResult = "N/A"
    if ($bits -eq 8) {
        $null = Invoke-GuestShell "START $GuestJob\V9XPAL.EXE /auto"
        $palette = $null
        for ($attempt = 0; $attempt -lt 20; ++$attempt) {
            Start-Sleep -Milliseconds 500
            $candidate = Invoke-GuestShell (
                "IF EXIST C:\V9XDIAG\V9XPAL.INI TYPE C:\V9XDIAG\V9XPAL.INI")
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

    # Scanout check - see the block above the mode loop.
    $scanout = "SKIPPED-not-qemu"
    if ($script:V9xEmulator -eq 'qemu') {
        $ppm = Join-Path $modeResults "scanout.ppm"
        # .Replace, not -replace: the latter takes a regex, and a lone backslash
        # is not a valid one. Forward slashes because the QEMU monitor parses
        # backslashes in its own argument.
        $ppmForMonitor = $ppm.Replace('\', '/')
        if (Invoke-V9xQemuMonitor "screendump $ppmForMonitor" $MonitorPort) {
            if (Test-Path -LiteralPath $ppm) {
                $measured = Measure-V9xPpmColors $ppm
                if ($measured.Width -ne $width -or $measured.Height -ne $height) {
                    throw ("Mode {0} scanout is {1}x{2}, expected {3}x{4}." -f
                           $name, $measured.Width, $measured.Height,
                           $width, $height)
                }
                if ($measured.DistinctColors -lt $script:V9xScanoutMinColors) {
                    # Interpolation, not -f: with a concatenated message the -f
                    # binds to the last literal only, which is why this check
                    # printed a message full of {0} placeholders the first time
                    # it actually fired.
                    $issue = 'docs\issues\2026-08-27-vbe-1024x768x16-scanout-stripes.md'
                    throw ("Mode $name scanout has only " +
                           "$($measured.DistinctColors) distinct colours, " +
                           "below $($script:V9xScanoutMinColors): the displayed " +
                           "image is not a desktop even though GDI readback " +
                           "passed. Capture kept at $ppm. See $issue.")
                }
                $scanout = "PASS-{0}-colors" -f $measured.DistinctColors
            } else {
                $scanout = "SKIPPED-no-screendump-file"
            }
        } else {
            $scanout = "SKIPPED-monitor-unreachable"
        }
    }
    $matrix += [pscustomobject]@{
        Pass = $pass
        Mode = $name
        AppliedBy = $appliedBy
        BootCounter = $info.BootCounter
        DriverStage = "enable-ok"
        GdiResult = "PASS"
        AccelResult = $accelResult
        PaletteResult = $paletteResult
        ScanoutResult = $scanout
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
    # Recorded at the top level as well as per mode: a summary that does not say
    # how its modes were applied invites a live-switch run being read as full
    # coverage, which it is not - see the -LiveSwitch note in the parameters.
    AppliedBy = $(if ($LiveSwitch) { "live-switch" } else { "reboot" })
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
    if ($LiveSwitch) {
        Write-Output ("Applied by live mode switch: this run did not exercise " +
                      "establishing each mode at boot.")
    }
}

# Arm the Intel Phase 4 first-write experiment on the live USB stick.
#
# Reads the unarmed boot's INTELRNG.TXT, validates that capture and the Phase
# 1-3 captures beside it, then writes the [Velocity9x] arm keys into the
# stick's WINDOWS\SYSTEM.INI. Hand-transcribing an eight-digit CRC and a build
# id onto a machine that must then be booted blind is where an operator error
# costs a whole trip, so nothing here is typed twice.
#
# Dry run (writes nothing, prints the block it would install):
#   .\scripts\arm-intel-phase4.ps1 -Capture <usb-copy>\INTELRNG.TXT -StickRoot E:
# Arm:
#   .\scripts\arm-intel-phase4.ps1 -Capture <usb-copy>\INTELRNG.TXT -StickRoot E: -Confirm
# Return the stick to unarmed:
#   .\scripts\arm-intel-phase4.ps1 -StickRoot E: -Disarm -Confirm
[CmdletBinding(DefaultParameterSetName = 'Arm')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Arm')]
    [string]$Capture,
    [Parameter(Mandatory = $true, ParameterSetName = 'Arm')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Disarm')]
    [string]$StickRoot,
    [Parameter(ParameterSetName = 'Arm')]
    [string]$Token,
    [Parameter(ParameterSetName = 'Arm')]
    [switch]$SkipCaptureChecks,
    [Parameter(Mandatory = $true, ParameterSetName = 'Disarm')]
    [switch]$Disarm,
    [Parameter(ParameterSetName = 'Arm')]
    [Parameter(ParameterSetName = 'Disarm')]
    [switch]$AcknowledgeIncomplete,
    [Parameter(ParameterSetName = 'Arm')]
    [Parameter(ParameterSetName = 'Disarm')]
    [switch]$Confirm,
    [Parameter(Mandatory = $true, ParameterSetName = 'SelfTest')]
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$armSection = 'Velocity9x'

# ---------------------------------------------------------------------------
# INI editing. SYSTEM.INI is a Win98 boot file: every line this script did not
# set is preserved byte for byte, the file keeps CRLF and ASCII with no BOM,
# and the previous contents are copied aside before the first write.
# ---------------------------------------------------------------------------
function Read-V9xIniLines {
    param([string]$Path)
    $text = [IO.File]::ReadAllText($Path, [Text.Encoding]::ASCII)
    return , ($text -split "`r`n|`n|`r")
}

function Get-V9xIniSectionValues {
    param([string[]]$Lines, [string]$Section)
    $values = @{}
    $inSection = $false
    foreach ($line in $Lines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') {
            $inSection = $Matches[1] -ieq $Section
            continue
        }
        if (-not $inSection -or -not $trimmed -or
            $trimmed.StartsWith(';')) { continue }
        $separator = $trimmed.IndexOf('=')
        if ($separator -le 0) { continue }
        $key = $trimmed.Substring(0, $separator).Trim()
        $values[$key] = $trimmed.Substring($separator + 1).Trim()
    }
    return $values
}

# Returns the new line array. Keys present in the section are rewritten in
# place; keys absent are appended to the end of the section; a missing section
# is appended to the file.
function Set-V9xIniValues {
    param([string[]]$Lines, [string]$Section, [System.Collections.IDictionary]$Values)
    $result = [System.Collections.Generic.List[string]]::new()
    $pending = [ordered]@{}
    foreach ($key in $Values.Keys) { $pending[$key] = $Values[$key] }

    $inSection = $false
    $sectionSeen = $false
    $lastSectionLine = -1
    foreach ($line in $Lines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') {
            if ($inSection) {
                # Leaving our section: flush whatever was not already present.
                foreach ($key in @($pending.Keys)) {
                    $result.Insert($lastSectionLine + 1, "$key=$($pending[$key])")
                    ++$lastSectionLine
                    $pending.Remove($key)
                }
            }
            $inSection = $Matches[1] -ieq $Section
            if ($inSection) { $sectionSeen = $true }
            $result.Add($line)
            if ($inSection) { $lastSectionLine = $result.Count - 1 }
            continue
        }
        if ($inSection) {
            $separator = $trimmed.IndexOf('=')
            if ($separator -gt 0 -and -not $trimmed.StartsWith(';')) {
                $key = $trimmed.Substring(0, $separator).Trim()
                $match = @($pending.Keys | Where-Object { $_ -ieq $key })
                if ($match.Count -eq 1) {
                    $result.Add("$($match[0])=$($pending[$match[0]])")
                    $pending.Remove($match[0])
                    $lastSectionLine = $result.Count - 1
                    continue
                }
            }
            $result.Add($line)
            if ($trimmed) { $lastSectionLine = $result.Count - 1 }
            continue
        }
        $result.Add($line)
    }

    if ($inSection) {
        foreach ($key in @($pending.Keys)) {
            $result.Insert($lastSectionLine + 1, "$key=$($pending[$key])")
            ++$lastSectionLine
            $pending.Remove($key)
        }
    }
    if (-not $sectionSeen) {
        while ($result.Count -gt 0 -and -not $result[$result.Count - 1]) {
            $result.RemoveAt($result.Count - 1)
        }
        $result.Add('')
        $result.Add("[$Section]")
        foreach ($key in @($pending.Keys)) {
            $result.Add("$key=$($pending[$key])")
            $pending.Remove($key)
        }
        $result.Add('')
    }
    if ($pending.Count -ne 0) {
        throw "Internal error: $($pending.Count) arm key(s) were not written."
    }
    return , $result.ToArray()
}

function Write-V9xIniLines {
    param([string]$Path, [string[]]$Lines)
    $text = ($Lines -join "`r`n")
    [IO.File]::WriteAllText($Path, $text, (New-Object Text.ASCIIEncoding))
}

# ---------------------------------------------------------------------------
# Self-test: the INI rewrite is the only part that can silently corrupt a boot
# file, so it is the part with a fixture.
# ---------------------------------------------------------------------------
if ($PSCmdlet.ParameterSetName -eq 'SelfTest') {
    $temporary = Join-Path ([IO.Path]::GetTempPath()) `
        ('v9x-arm-' + [Guid]::NewGuid().ToString('N') + '.ini')
    try {
        $original = @(
            '[boot]', 'display.drv=v9xdisp.drv', '', '[386Enh]',
            'MaxPhysPage=20000', 'device=*vdd', '', '[Velocity9x]',
            'IntelAccelDefault=0', 'IntelArmOnce=', 'IntelEnableThisBoot=0',
            '', '[vcache]', 'MaxFileCache=262144', '')
        Write-V9xIniLines -Path $temporary -Lines $original
        $keys = [ordered]@{
            IntelAccelDefault = '0'; IntelArmOnce = 'p4-test'
            IntelArmCrc = '3EAA137B'; IntelArmBuildId = 'abc1234'
            IntelInFlight = ''; IntelEnableThisBoot = '0'
            IntelLastResult = ''
        }
        $updated = Set-V9xIniValues -Lines (Read-V9xIniLines $temporary) `
            -Section $armSection -Values $keys
        Write-V9xIniLines -Path $temporary -Lines $updated
        $back = Read-V9xIniLines $temporary
        $values = Get-V9xIniSectionValues -Lines $back -Section $armSection
        foreach ($key in $keys.Keys) {
            if ($values[$key] -cne $keys[$key]) {
                throw "Self-test key $key round-tripped as '$($values[$key])'."
            }
        }
        # Every foreign line survives, exactly once, in its original order.
        foreach ($line in @('[boot]', 'display.drv=v9xdisp.drv', '[386Enh]',
                            'MaxPhysPage=20000', 'device=*vdd', '[vcache]',
                            'MaxFileCache=262144')) {
            if (@($back | Where-Object { $_ -ceq $line }).Count -ne 1) {
                throw "Self-test lost or duplicated the foreign line '$line'."
            }
        }
        if ([Array]::IndexOf($back, '[386Enh]') -ge
            [Array]::IndexOf($back, '[vcache]')) {
            throw 'Self-test reordered the foreign sections.'
        }
        # A file with no [Velocity9x] section gains one without losing anything.
        Write-V9xIniLines -Path $temporary -Lines @('[boot]', 'display.drv=vga.drv', '')
        $created = Set-V9xIniValues -Lines (Read-V9xIniLines $temporary) `
            -Section $armSection -Values $keys
        $values = Get-V9xIniSectionValues -Lines $created -Section $armSection
        if ($values['IntelArmCrc'] -cne '3EAA137B' -or
            @($created | Where-Object { $_ -ceq 'display.drv=vga.drv' }).Count -ne 1) {
            throw 'Self-test failed to append a missing [Velocity9x] section.'
        }
        # Disarm clears only what it names.
        $cleared = Set-V9xIniValues -Lines $updated -Section $armSection `
            -Values ([ordered]@{ IntelArmOnce = ''; IntelEnableThisBoot = '0' })
        $values = Get-V9xIniSectionValues -Lines $cleared -Section $armSection
        if ($values['IntelArmOnce'] -cne '' -or
            $values['IntelArmCrc'] -cne '3EAA137B') {
            throw 'Self-test disarm changed the wrong keys.'
        }
        Write-Output 'Intel Phase 4 arm-script self-test passed (rewrite, append, disarm).'
    } finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
    exit 0
}

# ---------------------------------------------------------------------------
# Locate the stick and its SYSTEM.INI.
# ---------------------------------------------------------------------------
$stick = (Resolve-Path -LiteralPath $StickRoot).Path
$systemIni = Join-Path $stick 'WINDOWS\SYSTEM.INI'
if (-not (Test-Path -LiteralPath $systemIni -PathType Leaf)) {
    throw "No WINDOWS\SYSTEM.INI under $stick. Is that the live USB stick?"
}
$current = Get-V9xIniSectionValues -Lines (Read-V9xIniLines $systemIni) `
    -Section $armSection
$inFlight = if ($current.ContainsKey('IntelInFlight')) { $current['IntelInFlight'] } else { '' }
if ($inFlight -and -not $AcknowledgeIncomplete) {
    throw ("The stick still carries IntelInFlight=$inFlight, so the previous " +
           "attempt did not complete. Collect its V9XDIAG first. Re-run with " +
           "-AcknowledgeIncomplete only when the risk decision's single " +
           "identical retry is intended.")
}

if ($Disarm) {
    $keys = [ordered]@{ IntelArmOnce = ''; IntelEnableThisBoot = '0' }
    if ($AcknowledgeIncomplete) { $keys['IntelInFlight'] = '' }
    Write-Host "Disarm $systemIni" -ForegroundColor Cyan
    foreach ($key in $keys.Keys) { Write-Host ("  {0}={1}" -f $key, $keys[$key]) }
    if (-not $Confirm) {
        Write-Host 'Dry run: nothing written. Re-run with -Confirm.' -ForegroundColor Yellow
        exit 0
    }
} else {
    # -----------------------------------------------------------------------
    # Validate the unarmed capture and the Phase 1-3 captures beside it.
    # -----------------------------------------------------------------------
    $capturePath = (Resolve-Path -LiteralPath $Capture).Path
    $captureDirectory = Split-Path -Parent $capturePath
    # -Json, because the validator's default output is formatting objects.
    $plan = & (Join-Path $PSScriptRoot 'check-intel-ring-plan.ps1') `
        -Path $capturePath -Json | ConvertFrom-Json
    if ($plan.Result -cne 'ERRATA-GATED') {
        throw "The capture is not an unarmed ring plan: Result=$($plan.Result)."
    }
    if (-not $SkipCaptureChecks) {
        $mmio = Join-Path $captureDirectory 'INTELMM.TXT'
        $gtt = Join-Path $captureDirectory 'INTELGTT.TXT'
        $events = Join-Path $captureDirectory 'INTELEVT.TXT'
        foreach ($required in @($mmio, $gtt, $events)) {
            if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
                throw ("$required is missing. Arm against a complete V9XDIAG " +
                       "copy, or pass -SkipCaptureChecks and say so in the " +
                       "write-up.")
            }
        }
        $null = & (Join-Path $PSScriptRoot 'check-intel-mmio-capture.ps1') -Path $mmio
        $null = & (Join-Path $PSScriptRoot 'check-intel-gtt-capture.ps1') -Path $gtt
        $null = & (Join-Path $PSScriptRoot 'check-intel-event-capture.ps1') -Path $events
    }

    $planValues = Get-V9xIniSectionValues -Lines (Read-V9xIniLines $capturePath) `
        -Section 'IntelRing'
    $buildId = $planValues['CaptureBuildId']
    $crc = $plan.ArmExecutionCrc
    if (-not $buildId) { throw 'The capture has no CaptureBuildId.' }
    if ($crc -notmatch '^[0-9A-F]{8}$') { throw "Implausible ArmExecutionCrc '$crc'." }

    # The build that will run must be the build that was captured. Arming a
    # CRC produced by one package while another is installed is exactly the
    # mistake this script exists to prevent.
    $manifest = Join-Path $stick 'INTELGMA\MANIFEST.TXT'
    if (Test-Path -LiteralPath $manifest -PathType Leaf) {
        $stickBuild = (Select-String -LiteralPath $manifest -Pattern '^Build:\s*(.+)$' |
            Select-Object -First 1).Matches.Groups[1].Value.Trim()
        if ($stickBuild -and $stickBuild -cne $buildId) {
            throw ("The stick's INTELGMA package is build $stickBuild but the " +
                   "capture came from $buildId. Re-deploy, re-run the unarmed " +
                   "boot, and arm against that capture.")
        }
    } else {
        Write-Host "Note: no INTELGMA\MANIFEST.TXT on the stick; build id not cross-checked." `
            -ForegroundColor Yellow
    }

    if (-not $Token) { $Token = 'p4-{0:yyyyMMdd}-a' -f (Get-Date) }
    if ($Token -notmatch '^[A-Za-z0-9._-]{1,63}$') {
        throw "Token '$Token' is not the token alphabet the arm contract accepts."
    }
    if ($current.ContainsKey('IntelLastResult') -and
        $current['IntelLastResult'] -like "*:$Token") {
        throw ("Token $Token already has a recorded result " +
               "($($current['IntelLastResult'])). Use a new token.")
    }

    $keys = [ordered]@{
        IntelAccelDefault  = '0'
        IntelArmOnce       = $Token
        IntelArmCrc        = $crc
        IntelArmBuildId    = $buildId
        IntelInFlight      = ''
        IntelEnableThisBoot = '0'
        IntelLastResult    = ''
    }
    Write-Host "Arm $systemIni" -ForegroundColor Cyan
    Write-Host ("  capture      {0}" -f $capturePath)
    Write-Host ("  ring/HWS/scr {0} / {1} / {2}" -f $plan.RingOffset, $plan.HwsOffset, $plan.ScratchOffset)
    Write-Host ''
    Write-Host "[$armSection]"
    foreach ($key in $keys.Keys) { Write-Host ("{0}={1}" -f $key, $keys[$key]) }
    Write-Host ''
    if (-not $Confirm) {
        Write-Host 'Dry run: nothing written. Re-run with -Confirm.' -ForegroundColor Yellow
        exit 0
    }
}

# ---------------------------------------------------------------------------
# Write, keeping a copy of what was there.
# ---------------------------------------------------------------------------
$backup = Join-Path $stick 'WINDOWS\SYSTEM.V9X'
if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
    Copy-Item -LiteralPath $systemIni -Destination $backup
    Write-Host "Kept the previous SYSTEM.INI as WINDOWS\SYSTEM.V9X"
}
$updated = Set-V9xIniValues -Lines (Read-V9xIniLines $systemIni) `
    -Section $armSection -Values $keys
Write-V9xIniLines -Path $systemIni -Lines $updated

$verify = Get-V9xIniSectionValues -Lines (Read-V9xIniLines $systemIni) `
    -Section $armSection
foreach ($key in $keys.Keys) {
    if ($verify[$key] -cne $keys[$key]) {
        throw "Read-back of $key gave '$($verify[$key])', expected '$($keys[$key])'."
    }
}
Write-Host ''
if ($Disarm) {
    Write-Host 'Stick disarmed and verified. The next boot runs unarmed.' -ForegroundColor Green
} else {
    Write-Host 'Stick armed and verified.' -ForegroundColor Green
    Write-Host 'Boot it once on AC, wait for the desktop, photograph it, shut down,'
    Write-Host 'then collect V9XDIAG and validate with:'
    Write-Host '  .\scripts\check-intel-ring-plan.ps1 -Path <usb-copy>\INTELRNG.TXT -Armed'
}

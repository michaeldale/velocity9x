# Per-family enable smoke gate.
#
# run-checks builds every family package and passes, because a package that
# builds is not a package that enables. That is precisely how the ati package
# shipped unable to enable for a release
# (docs\issues\2026-08-26-ati-package-cannot-enable.md): every check it had to
# pass was a check it could pass without working.
#
# This gate closes that hole with the smallest thing that can: start each
# family's emulated guest, deploy the freshly built package over the already
# associated driver, reboot, and assert Stage=enable-ok. Nothing else. It is not
# the mode matrix - it does not switch modes, run V9XGDI or compare pixels -
# because the point is one command that is cheap enough to run before any merge
# that touches the shared 16-bit layer, where a mistake is a four-family
# mistake.
#
# Deliberately NOT part of run-checks: it needs virtual machines and minutes.
# See docs\BUILDING.md for when to run it.
#
# Coverage honesty: this reaches three families, not four, and it prints the
# ones it skips by name rather than quietly equating "each family with an
# emulator" with "every family".
[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = "C:\98DDK",
    # Limit the run to these families. Default is every family that declares an
    # emulator.
    [string[]]$Family,
    # Path to the remote-agent controller (v9xctl.ps1). Set V9X_AGENT_CTL to
    # avoid passing it on every call; the agent lives outside this repository.
    [string]$ControllerPath = $env:V9X_AGENT_CTL,
    # 86Box installation and profile directory. Both are external project
    # inputs; see docs\vm-environment.md.
    [string]$EmulatorPath = "C:\86Box\86Box.exe",
    [string]$ProfileRoot = (Join-Path $env:USERPROFILE "86Box VMs"),
    # Reuse whatever is already running and answering rather than starting a
    # guest. Faster on a machine whose VMs are already up.
    [switch]$NoStart,
    # Leave the guests running afterwards.
    [switch]$KeepRunning,
    [ValidateRange(60, 900)]
    [int]$AgentTimeoutSeconds = 300,
    [ValidateRange(30, 600)]
    [int]$BootTimeoutSeconds = 240,
    # Skip the package build and gate whatever is already in build\.
    [switch]$SkipBuild,
    [switch]$Json
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "lib\family.ps1")
. (Join-Path $PSScriptRoot "common.ps1")

if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "enable-gate-local"
}
if (-not $ControllerPath) {
    throw ("Specify -ControllerPath (or set V9X_AGENT_CTL) to the remote " +
           "agent's v9xctl.ps1.")
}
if (-not (Test-Path -LiteralPath $ControllerPath)) {
    throw "Required path does not exist: $ControllerPath"
}
$powershell = Join-Path $PSHOME "powershell.exe"

$families = @(Get-V9xFamilies -RepoRoot $repoRoot)
if ($Family) {
    $unknown = @($Family | Where-Object { $_ -notin @($families.Id) })
    if ($unknown.Count -ne 0) {
        throw ("Unknown family: " + ($unknown -join ', ') + ". Known: " +
               (@($families.Id) -join ', ') + ".")
    }
    $families = @($families | Where-Object { $_.Id -in $Family })
}

# ---------------------------------------------------------------------------
# Which targets this gate can reach, and which it cannot.
#
# A family can be part emulated and part physical - the ati family pairs a
# Mach64 VT2 that 86Box emulates with a Rage Mobility that nothing does - so the
# decision is per target, not per family. The refusal wording is the same one
# run-vm-mode-matrix.ps1 gives (:36-50), because a caller who has read one
# should not have to learn a second.
# ---------------------------------------------------------------------------

$covered = @()
$skipped = @()
foreach ($familyManifest in $families) {
    if ($familyManifest.Vm.Emulator -eq 'none') {
        $skipped += [pscustomobject]@{
            Family = $familyManifest.Id
            ChipId = ''
            Reason = ("Family $($familyManifest.Id) declares no emulator: it " +
                      "is validated on physical hardware only. There is no VM " +
                      "to gate against.")
        }
        continue
    }
    $chipIds = @(@($familyManifest.Vm.Targets | Where-Object { $_ }) |
        ForEach-Object { $_.ChipId })
    if ($chipIds.Count -eq 0) {
        $chipIds = @($null)
    }
    foreach ($chipId in $chipIds) {
        $target = Get-V9xFamilyVmTarget -Family $familyManifest -ChipId $chipId
        if ($target.Emulator -eq 'none') {
            $skipped += [pscustomobject]@{
                Family = $familyManifest.Id
                ChipId = $target.ChipId
                Reason = ("Family $($familyManifest.Id) chip " +
                          "'$($target.ChipId)' declares no emulator: it is " +
                          "validated on physical hardware only.")
            }
            continue
        }
        $emulator = if ($target.Emulator) { $target.Emulator }
                    else { $familyManifest.Vm.Emulator }
        $covered += [pscustomobject]@{
            Family = $familyManifest.Id
            Manifest = $familyManifest
            ChipId = $target.ChipId
            Profile = $target.Profile
            Port = [int]$target.Port
            Emulator = $emulator
            PackagePath = Join-Path $repoRoot ("build\{0}" -f
                (Split-Path -Leaf $familyManifest.Build.PackageOutput))
        }
    }
}

Write-Output ("Enable gate covers {0} target(s): {1}" -f $covered.Count,
    ((@($covered | ForEach-Object { "$($_.Family)/$($_.ChipId)" })) -join ', '))
if ($skipped.Count -eq 0) {
    Write-Output "No targets skipped."
} else {
    Write-Output "Skipped, by name:"
    foreach ($skip in $skipped) {
        $label = if ($skip.ChipId) { "$($skip.Family)/$($skip.ChipId)" }
                 else { $skip.Family }
        Write-Output "  $label - $($skip.Reason)"
    }
}
if ($covered.Count -eq 0) {
    throw "No emulated target to gate. Nothing was verified."
}

# ---------------------------------------------------------------------------
# Guest lifecycle.
#
# Two mechanics worth having in the script rather than rediscovering:
#
#   - The forwarded host port opens BEFORE the guest agent listens. A TCP
#     connect therefore succeeds against a machine that is still in POST, and
#     the next controller call fails with something that says nothing about
#     why. Poll `v9xctl ping` instead: it only answers once the agent is up.
#   - A guest that wedges recovers with a force-kill and a restart, and a
#     committed WININIT driver install survives it - so a retry is a real
#     recovery and not a lost deployment.
# ---------------------------------------------------------------------------

function Test-V9xAgent {
    param([int]$Port)
    $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                   $ControllerPath, "ping", "-Json", "-Port", [string]$Port)
    try {
        $lines = @(& $powershell @arguments)
        if ($LASTEXITCODE -ne 0) { return $false }
    } catch {
        return $false
    }
    $jsonLine = $lines | Where-Object {
        $_ -is [string] -and $_.TrimStart().StartsWith("{")
    } | Select-Object -Last 1
    if (-not $jsonLine) { return $false }
    try {
        return ($jsonLine | ConvertFrom-Json).Success -eq $true
    } catch {
        return $false
    }
}

function Get-V9xEmulatorProcess {
    param([string]$Profile)
    # 86Box sets its window title from the profile name once the VM is really
    # running, which is also the signal that the "moved or copied" modal is not
    # sitting in front of it (docs\vm-environment.md).
    @(Get-Process -Name "86Box" -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowTitle -like "*$Profile*" })
}

# Returns 'answering' when the agent is already up, 'starting' when an emulator
# is already open on the profile, and 'started' when this call launched it.
#
# A STRING, and all progress reporting done by the caller, deliberately: a
# Write-Output inside a PowerShell function joins that function's output
# stream, so `$started = Start-V9xGuest ...` captures the message as well as
# the value and every caller sees a truthy array. That is not hypothetical - it
# is what made the first run of this script force-kill three guests it had not
# started.
function Start-V9xGuest {
    param([string]$Profile, [int]$Port, [string]$Emulator)

    if (Test-V9xAgent -Port $Port) {
        return 'answering'
    }
    if ($NoStart) {
        throw ("No agent is answering on port $Port and -NoStart was given, " +
               "so profile '$Profile' was not started.")
    }
    # This gate knows how to start 86Box and nothing else. The vbe family's
    # guest is QEMU, and inventing a launch for it here would be a second,
    # untested way to start a VM - so it is required to be up already, and the
    # message says which guest and why rather than failing on a port.
    if ($Emulator -ne '86box') {
        throw ("The guest for this target runs under '$Emulator', which this " +
               "gate does not launch: it starts 86Box profiles only. Bring " +
               "'$Profile' up so its agent answers on port $Port, then re-run.")
    }
    # An 86Box instance already open on this profile means the guest is
    # booting, not that it needs starting. Launching a second one against the
    # same profile directory puts two emulators on one VHD, which is a way to
    # lose a guest rather than to test one.
    if (@(Get-V9xEmulatorProcess -Profile $Profile).Count -ne 0) {
        return 'starting'
    }
    $profilePath = Join-Path $ProfileRoot $Profile
    foreach ($path in @($EmulatorPath, $profilePath)) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Required emulator input does not exist: $path"
        }
    }
    # One pre-quoted string: -ArgumentList does not re-quote, and the profile
    # root contains a space, so an array would be split and 86Box would
    # silently open something else (docs\vm-environment.md).
    Start-Process -FilePath $EmulatorPath `
        -ArgumentList ("-P `"$profilePath`"") | Out-Null
    return 'started'
}

# Same contract discipline as Start-V9xGuest: this returns a bool that a caller
# tests, so it must not write to the output stream. `-not @('message', $true)`
# is $false, which would turn a wedged guest into a silent pass. The one thing
# worth reporting is recorded here and printed by the caller.
$script:V9xForceRestarted = $false
function Wait-V9xAgent {
    param([string]$Profile, [int]$Port, [int]$TimeoutSeconds)

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    # One force-restart, at the halfway mark. A guest that has not produced an
    # agent by then is wedged rather than slow, and a committed WININIT driver
    # install survives a force-kill, so the retry cannot lose a deployment.
    $retryAt = (Get-Date).AddSeconds([int]($TimeoutSeconds / 2))
    $restarted = $false
    while ((Get-Date) -lt $deadline) {
        if (Test-V9xAgent -Port $Port) {
            return $true
        }
        if (-not $restarted -and -not $NoStart -and (Get-Date) -gt $retryAt) {
            $restarted = $true
            $processes = @(Get-V9xEmulatorProcess -Profile $Profile)
            if ($processes.Count -ne 0) {
                $script:V9xForceRestarted = $true
                $processes | Stop-Process -Force
                Start-Sleep -Seconds 3
            }
            $null = Start-V9xGuest -Profile $Profile -Port $Port `
                                  -Emulator '86box'
        }
        Start-Sleep -Seconds 5
    }
    return $false
}

# ---------------------------------------------------------------------------
# Build, deploy, assert.
# ---------------------------------------------------------------------------

if (-not $SkipBuild) {
    Write-Output "==> building family packages"
    $buildArguments = @{ BuildId = $BuildId; DdkRoot = $DdkRoot }
    $buildFamilies = @($covered | ForEach-Object { $_.Family } |
        Sort-Object -Unique)
    $buildArguments['Family'] = $buildFamilies
    & (Join-Path $PSScriptRoot "build-all-packages.ps1") @buildArguments |
        Out-Null
}

$results = Join-Path $repoRoot ("build\driver-results\enable-gate-{0}" -f
    (Get-Date -Format "yyyyMMdd-HHmmss"))
New-Item -ItemType Directory -Force -Path $results | Out-Null

$outcomes = @()
foreach ($target in $covered) {
    Write-Output ("==> {0}/{1} on '{2}' port {3}" -f $target.Family,
        $target.ChipId, $target.Profile, $target.Port)
    if (-not (Test-Path -LiteralPath $target.PackagePath)) {
        throw ("Family $($target.Family) package is missing: " +
               "$($target.PackagePath)")
    }
    $startState = Start-V9xGuest -Profile $target.Profile `
                                -Port $target.Port `
                                -Emulator $target.Emulator
    # 'started' is the only state this run owns and therefore the only one it
    # may shut down afterwards.
    $started = $startState -eq 'started'
    Write-Output ("  " + $(switch ($startState) {
        'started' { "started 86Box profile '$($target.Profile)'" }
        'starting' { "86Box is already open on '$($target.Profile)'; waiting" }
        default { "agent on $($target.Port) already answering" }
    }))
    $script:V9xForceRestarted = $false
    $agentUp = Wait-V9xAgent -Profile $target.Profile -Port $target.Port `
                             -TimeoutSeconds $AgentTimeoutSeconds
    if ($script:V9xForceRestarted) {
        Write-Output ("  no agent after half the budget; force-restarted " +
                      "'$($target.Profile)'")
    }
    if (-not $agentUp) {
        throw ("The guest agent on port $($target.Port) ('$($target.Profile)') " +
               "did not answer a ping within $AgentTimeoutSeconds seconds. " +
               "Note that the forwarded host port opens before the agent " +
               "listens, so a reachable port is not a running agent.")
    }

    $jobId = "enable-gate-{0}-{1}" -f $target.Family, $target.ChipId
    $deployResults = Join-Path $results ("{0}-{1}" -f $target.Family,
        $target.ChipId)
    & (Join-Path $PSScriptRoot "update-associated-driver.ps1") `
        -PackagePath $target.PackagePath -ControllerPath $ControllerPath `
        -Port $target.Port -JobId $jobId -ResultsDirectory $deployResults `
        -BootTimeoutSeconds $BootTimeoutSeconds | Out-Null

    # The one assertion this gate makes. Same key and same wording as
    # run-vm-mode-matrix.ps1:227, because it is the same claim.
    $traceArguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                        $ControllerPath, "shell", "-Json",
                        "-Port", [string]$target.Port,
                        "-ShellCommand", "TYPE C:\V9XBOOT.INI")
    $traceLines = @(& $powershell @traceArguments)
    $traceJson = $traceLines | Where-Object {
        $_ -is [string] -and $_.TrimStart().StartsWith("{")
    } | Select-Object -Last 1
    if (-not $traceJson) {
        throw ("Could not read C:\V9XBOOT.INI from " +
               "$($target.Family)/$($target.ChipId).")
    }
    $trace = $traceJson | ConvertFrom-Json
    Set-Content -LiteralPath (Join-Path $deployResults "V9XBOOT.INI") `
        -Value $trace.Stdout -Encoding Ascii
    if ($trace.Stdout -notmatch '(?m)^Stage=enable-ok\s*$') {
        throw ("$($target.Family)/$($target.ChipId) did not reach the " +
               "enable-ok driver trace. C:\V9XBOOT.INI said:" +
               [Environment]::NewLine + $trace.Stdout)
    }
    Write-Output "  Stage=enable-ok"
    $outcomes += [pscustomobject]@{
        Family = $target.Family
        ChipId = $target.ChipId
        Profile = $target.Profile
        Port = $target.Port
        Started = $started
        Stage = "enable-ok"
    }
    # Only ever stop a guest this run started. Killing one that was already up
    # takes a machine away from whoever was using it, and it is why the return
    # value above is not a boolean with a Write-Output next to it.
    if (-not $KeepRunning -and $started) {
        @(Get-V9xEmulatorProcess -Profile $target.Profile) |
            Stop-Process -Force -ErrorAction SilentlyContinue
    }
}

$summary = [pscustomobject]@{
    Success = $true
    BuildId = $BuildId
    ResultsDirectory = $results
    Covered = $outcomes
    Skipped = $skipped
}
$summary | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $results "enable-gate.json") `
        -Encoding UTF8
if ($Json) {
    $summary | ConvertTo-Json -Depth 6 -Compress
} else {
    $outcomes | Format-Table -AutoSize
    # One string, then -f. Concatenating first and formatting after binds the
    # operator to the trailing literal alone, which prints the placeholders.
    Write-Output (("Enable gate passed on {0} target(s); {1} skipped. " +
                   "Results: {2}") -f $outcomes.Count, $skipped.Count, $results)
}

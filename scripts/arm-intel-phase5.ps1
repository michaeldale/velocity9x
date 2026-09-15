# Arm the Intel Phase 5 triangle on the live USB stick.
#
# A SEPARATE script from arm-intel-phase4.ps1, deliberately. One armer that
# could arm either phase would be a single flag away from arming the wrong one,
# and the two phases are not equivalent: the 2026-09-13 risk decision opens the
# errata gate for Phase 4 specifically, on an argument about minimal
# processor-to-graphics interaction that does not transfer to a 3D draw. Phase 5
# needs its own dated decision, and this script refuses without one.
#
# The armed CRC is the COMBINED one covering the Phase 4 replay and the Phase 5
# draw in execution order, taken from the generated table rather than typed.
# Hand-transcribing an eight-digit CRC onto a machine that must then be booted
# blind is where an operator error costs a whole trip.
#
# Dry run (writes nothing, prints the block it would install):
#   .\scripts\arm-intel-phase5.ps1 -StickRoot E:
# Arm:
#   .\scripts\arm-intel-phase5.ps1 -StickRoot E: -Confirm
# Return the stick to unarmed:
#   .\scripts\arm-intel-phase5.ps1 -StickRoot E: -Disarm -Confirm
[CmdletBinding(DefaultParameterSetName = 'Arm')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Arm')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Disarm')]
    [string]$StickRoot,
    [Parameter(ParameterSetName = 'Arm')]
    [string]$Token,
    [Parameter(ParameterSetName = 'Arm')]
    [string]$BuildId,
    [Parameter(Mandatory = $true, ParameterSetName = 'Disarm')]
    [switch]$Disarm,
    [Parameter(ParameterSetName = 'Arm')]
    [Parameter(ParameterSetName = 'Disarm')]
    [switch]$Confirm,
    [Parameter(Mandatory = $true, ParameterSetName = 'SelfTest')]
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$armSection = 'Velocity9x'
$generatedPath = Join-Path $repoRoot 'scripts\data\intel-3d-stream.psd1'
$decisionGlob = Join-Path $repoRoot 'docs\decisions\*-intel-phase5-errata-gate*.md'

function Get-V9xPhase5ArmBlock {
    if (-not (Test-Path -LiteralPath $generatedPath)) {
        throw ('scripts\data\intel-3d-stream.psd1 is missing. Run ' +
               'scripts\gen-intel-3d-stream.ps1 first; the arm CRC comes from ' +
               'the compiled builders, never from a keyboard.')
    }
    $generated = Import-PowerShellDataFile -LiteralPath $generatedPath
    foreach ($required in @('Combinedcrc', 'P4Crc', 'P5Crc')) {
        if (-not $generated.ContainsKey($required)) {
            throw "The generated stream table is missing $required."
        }
    }
    return [ordered]@{
        IntelArmPhase = '5'
        IntelArmCrc = $generated.Combinedcrc
        IntelArmPhase4Crc = $generated.P4Crc
        IntelArmPhase5Crc = $generated.P5Crc
    }
}

function Assert-V9xPhase5EratraGate {
    <#
      The governance check, and the reason this script exists separately.

      The errata gate was opened for Phase 4 on an argument its own record says
      does not transfer: that workload "did not provoke it, which says nothing
      about a heavier one". A triangle is a heavier one. So Phase 5 may not
      inherit that decision - it needs its own, dated, in docs\decisions.

      This refuses rather than warns. A warning on a script that is run once,
      under time pressure, on the day the hardware is free, is not a control.
    #>
    $decisions = @(Get-ChildItem -Path $decisionGlob -ErrorAction SilentlyContinue)
    if ($decisions.Count -eq 0) {
        throw ('Phase 5 has no errata-gate decision. The 2026-09-13 decision ' +
               'covers PHASE 4 ONLY, on an argument about minimal ' +
               'processor-to-graphics interaction that does not transfer to a ' +
               '3D draw - the Phase 4 record says so itself. Write ' +
               'docs\decisions\<date>-intel-phase5-errata-gate.md, taking the ' +
               'decision rather than inheriting it, before arming.')
    }
    return $decisions[0].Name
}

if ($SelfTest) {
    # The self-test proves the two things that would silently ruin a trip: that
    # the CRC comes from the generated table, and that the governance gate
    # actually refuses.
    $block = Get-V9xPhase5ArmBlock
    $generated = Import-PowerShellDataFile -LiteralPath $generatedPath
    if ($block['IntelArmCrc'] -cne $generated.Combinedcrc) {
        throw 'The Phase 5 armer did not take its CRC from the generated table.'
    }
    if ($block['IntelArmPhase'] -cne '5') {
        throw ('The Phase 5 armer must write IntelArmPhase=5, or a Phase 4 ' +
               'token could satisfy a Phase 5 run.')
    }
    if ($block['IntelArmCrc'] -ceq $generated.P4Crc -or
        $block['IntelArmCrc'] -ceq $generated.P5Crc) {
        throw ('The armed CRC must be the COMBINED one covering both streams ' +
               'in execution order, not either half.')
    }
    $gateRefused = $false
    try { $null = Assert-V9xPhase5EratraGate } catch { $gateRefused = $true }
    $decisions = @(Get-ChildItem -Path $decisionGlob -ErrorAction SilentlyContinue)
    if ($decisions.Count -eq 0 -and -not $gateRefused) {
        throw ('The Phase 5 armer accepted a run with no errata-gate decision ' +
               'on record.')
    }
    if ($decisions.Count -ne 0 -and $gateRefused) {
        throw 'The Phase 5 armer refused despite a decision being on record.'
    }
    $state = if ($decisions.Count -eq 0) { 'refuses (no decision on record)' }
             else { "accepts ($($decisions[0].Name))" }
    Write-Output ("Intel Phase 5 armer self-test passed: CRC " +
                  "$($block['IntelArmCrc']) from the generated table, " +
                  "errata gate $state.")
    return
}

$armPath = Join-Path $StickRoot 'V9XDIAG\INTELARM.TXT'

if ($Disarm) {
    $block = [ordered]@{ IntelArmOnce = ''; IntelEnableThisBoot = '0'
                         IntelArmPhase = '' }
} else {
    $decision = Assert-V9xPhase5EratraGate
    Write-Output "Phase 5 errata gate: $decision"
    $block = Get-V9xPhase5ArmBlock
    if (-not $Token) {
        $Token = 'phase5-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
    }
    $block['IntelArmOnce'] = $Token
    $block['IntelEnableThisBoot'] = '1'
    if ($BuildId) { $block['IntelArmBuildId'] = $BuildId }
}

Write-Output "Target: $armPath"
foreach ($key in $block.Keys) {
    Write-Output ("  {0}={1}" -f $key, $block[$key])
}
if (-not $Confirm) {
    Write-Output ''
    Write-Output 'Dry run. Nothing was written. Re-run with -Confirm to apply.'
    return
}
if (-not (Test-Path -LiteralPath (Split-Path -Parent $armPath))) {
    throw "No V9XDIAG directory at $StickRoot."
}
$lines = if (Test-Path -LiteralPath $armPath) {
    @(Get-Content -LiteralPath $armPath)
} else { @("[$armSection]") }
foreach ($key in $block.Keys) {
    $set = $false
    for ($index = 0; $index -lt $lines.Count; ++$index) {
        if ($lines[$index] -match ("^" + [regex]::Escape($key) + "=")) {
            $lines[$index] = "$key=$($block[$key])"
            $set = $true
        }
    }
    if (-not $set) { $lines += "$key=$($block[$key])" }
}
[IO.File]::WriteAllText($armPath, ($lines -join "`r`n") + "`r`n")
Write-Output "Wrote $armPath."

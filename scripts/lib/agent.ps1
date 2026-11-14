# The remote agent a test script drives, brought up to date before the test
# waits on it.
#
# Every script that drives a machine through v9xctl relies on verbs newer
# agents added to end waits at once: wait-desktop/reset -DismissLogon and
# reg -Set (0.17.0), wait-fault (0.16.0), exec -WaitForExit (0.14.0), cat,
# hash, windows (0.13.0). Older agents answer exit 23 or are refused, and the
# workarounds they forced were fixed sleeps and DOS commands. So an agent
# older than the agent repository's built install package is hot-updated
# first, and one still below the minimum afterwards stops the script.
#
# The package comes from the repository the controller lives in
# (<repo>\scripts\v9xctl.ps1 -> <repo>\build\install), and its version from
# that repository's include\v9xremote\version.h.

$script:V9xAgentMinimum = [version]'0.17.0'

function ConvertTo-V9xAgentVersion {
    param([string]$Text)

    # "0.10.0-cdb285e" and the like: the leading numbers are the version.
    if ($Text -match '^(\d+\.\d+\.\d+)') {
        return [version]$Matches[1]
    }
    return [version]'0.0.0'
}

function Get-V9xAgentPackage {
    param(
        [Parameter(Mandatory = $true)][string]$ControllerPath,
        [string]$Install
    )

    $agentRepo = Split-Path -Parent (Split-Path -Parent $ControllerPath)
    if (-not $Install) {
        $Install = Join-Path $agentRepo 'build\install'
    }
    $version = $script:V9xAgentMinimum
    $header = Join-Path $agentRepo 'include\v9xremote\version.h'
    if ((Test-Path -LiteralPath $header) -and
        (Get-Content -LiteralPath $header -Raw) -match '#define V9X_AGENT_VERSION "(\d+\.\d+\.\d+)') {
        $version = [version]$Matches[1]
    }
    return [pscustomobject]@{ Install = $Install; Version = $version }
}

# $Call is the script's own v9xctl wrapper: { param($Verb, [hashtable]$Arguments) }
# returning the parsed -Json result (or $null on failure). Returns the agent's
# info after any update. Throws when the agent is below the minimum.
function Update-V9xAgent {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Call,
        [Parameter(Mandatory = $true)]$Package,
        [switch]$NoUpdate,
        [scriptblock]$Log = { param($Text) Write-Verbose $Text },
        # The caller's own first info, when it has one: one call fewer.
        $KnownInfo
    )

    $info = $KnownInfo
    if (-not $info) {
        $info = & $Call 'info' @{}
    }
    if (-not $info -or -not $info.Success) {
        throw "The agent did not answer info."
    }
    # $Log's output goes to the host, never into this function's output,
    # which is the info object the caller reads.
    $version = ConvertTo-V9xAgentVersion $info.AgentVersion
    if ($version -lt $Package.Version -and -not $NoUpdate) {
        & $Log "updating agent $($info.AgentVersion) -> $($Package.Version)" | Out-Host
        $updated = & $Call 'update' @{ Source = $Package.Install; WaitSeconds = 180 }
        $info = & $Call 'info' @{}
        if (-not $info -or -not $info.Success) {
            throw "The agent did not answer after its update; look at the machine."
        }
        $version = ConvertTo-V9xAgentVersion $info.AgentVersion
        & $Log ("agent now $($info.AgentVersion)" +
            $(if (-not $updated -or -not $updated.Success) { ' (update reported a failure)' })) | Out-Host
    }
    if ($version -lt $script:V9xAgentMinimum) {
        throw ("Agent $($info.AgentVersion) is older than $script:V9xAgentMinimum, which this " +
               "script needs. Update it: v9xctl update -Source $($Package.Install)")
    }
    return $info
}

# Run the release checks on real machines: install each machine's family
# package, prove it installed, run the four probes, and write one evidence
# folder per machine plus a summary that says what changed since the last
# release's checks.
#
# Until 0.38.0 this was a script rewritten for every release in a session's
# scratch folder (relcheck.ps1, relcheck037.ps1); the 0.34.0 one is already
# gone. The traps it learned are kept here:
#   - The probes are rebuilt first, with the packages' build id. run-checks.ps1
#     does not build them, and a stale V9XR3DP reads FAIL-NEGOTIATE.
#   - The package is installed by WININIT.INI renames and a `reset`, never
#     NUL= lines or deletes (they stop Windows rebooting itself), and never
#     over a WININIT.INI that is already pending.
#   - A transport error right after `reset` can read as a hash mismatch, so a
#     mismatch is hashed once more before it is believed.
#   - V9XD32's switches start with '/', which Git Bash rewrites; they are only
#     ever passed inside PowerShell.
#
# Machines are independent, so each runs in its own PowerShell process and the
# whole run takes as long as the slowest machine. The machine list names hosts
# and remote-help relay ids, so it lives outside this repository: pass
# -MachineList or set V9X_RELEASE_MACHINES. Its format is described at the top
# of the file itself; in short, a .psd1 with ControllerPath and Machines, each
# machine a Name, Family, Host or Relay, and D32Runs.
[CmdletBinding()]
param(
    [string]$MachineList = $env:V9X_RELEASE_MACHINES,
    # Run only these machines (by Name). Default: every enabled machine.
    [string[]]$Machine,
    # Default: docs\probe\<version>-release-checks-<date>.
    [string]$OutDir,
    # An earlier release's evidence folder to compare against. Default: the
    # newest other docs\probe\*-release-checks-* folder.
    [string]$Previous,
    # Run the probes on the driver each machine has now: no install, no hash
    # check. Writes <name>-before folders, as the 0.37.0 checks did by hand.
    [switch]$Baseline,
    # Keep the probes already in build\ (they must carry the packages' build
    # id; the summary flags any that do not).
    [switch]$SkipProbeBuild,
    # Also run 3DMark 2000 on machines whose entry has a ThreeDMark sequence,
    # with V9XTRACE grabbing frames.
    [switch]$ThreeDMark,
    # Write SUMMARY.md for an existing -OutDir without running anything:
    # after rerunning one machine, or to compare two earlier folders.
    [switch]$SummaryOnly,
    # A machine that has not answered its first call by then is skipped and
    # the run goes on with the ones that did. A remote-help machine takes up
    # to 60 s to pick up a session (it checks in once a minute when idle).
    [ValidateRange(30, 900)]
    [int]$ReachSeconds = 65,
    # Only find out which machines answer: every machine in the list,
    # switched off or not, asked once in parallel, then a table. Nothing is
    # built, installed or run. Pick from it with -Machine.
    [switch]$List,
    # Internal: run one machine. The parent starts one process per machine.
    [string]$Worker
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
# powershell -File passes -Machine a,b as the single string "a,b".
$Machine = @($Machine | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
. (Join-Path $PSScriptRoot "common.ps1")

# The installed set: what WININIT.INI renames into C:\WINDOWS\SYSTEM. The
# staged names are 8.3 and distinct, so a half-finished install is visible
# as a staged file that is still there.
$installFiles = [ordered]@{
    'V9XDISP.DRV'  = 'C:\V9XNDRV.BIN'
    'V9XMINI.VXD'  = 'C:\V9XNVXD.BIN'
    'V9XHAL.DLL'   = 'C:\V9XNHAL.BIN'
    'V9XSETP.DLL'  = 'C:\V9XNSTP.BIN'
    'V9XGL.DLL'    = 'C:\V9XNGL.BIN'
    'GLIDE2X.DLL'  = 'C:\V9XNG2.BIN'
    'GLIDE3X.DLL'  = 'C:\V9XNG3.BIN'
    'ATI3DCIF.DLL' = 'C:\V9XNCIF.BIN'
}
$guestDir = 'C:\V9XREL'
# Probe executable -> the report it leaves in C:\V9XDIAG and the local name
# prefix the evidence folders have used since 0.34.0.
$probeReports = @(
    @{ Exe = 'V9XGLP.EXE'; Report = 'V9XGLP.INI'; Prefix = 'glp' }
    @{ Exe = 'V9XGLIDP.EXE'; Report = 'V9XGLIDP.INI'; Prefix = 'glidp' }
    @{ Exe = 'V9XR3DP.EXE'; Report = 'V9XR3DP.INI'; Prefix = 'r3dp' }
)

function Import-V9xMachineList {
    if (-not $MachineList) {
        throw ("Pass -MachineList or set V9X_RELEASE_MACHINES to the release " +
               "machine list (.psd1). It is kept outside the repository.")
    }
    if (-not (Test-Path -LiteralPath $MachineList)) {
        throw "Machine list not found: $MachineList"
    }
    $machineSet = Import-PowerShellDataFile -LiteralPath $MachineList
    foreach ($entry in $machineSet.Machines) {
        if (-not $entry.Name -or -not $entry.Family) {
            throw "Every machine in $MachineList needs Name and Family."
        }
        if ([bool]$entry.Host -eq [bool]$entry.Relay) {
            throw "Machine $($entry.Name) needs exactly one of Host and Relay."
        }
    }
    return $machineSet
}

# Every line of a probe report that is a verdict: the Result and Failures
# lines, any key ending in Result, and any value whose first word is a
# verdict (V9XD32's T4Flat=EXACT, V9XGLIDP's Triangle=FAIL read=...).
function Get-V9xProbeVerdicts {
    param([Parameter(Mandatory = $true)][string]$Path)

    $verdicts = [ordered]@{}
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        if ($line -notmatch '^([A-Za-z][A-Za-z0-9]*)=(.*)$') {
            continue
        }
        $key = $Matches[1]
        $value = $Matches[2].Trim()
        $word = ($value -split '\s+')[0]
        $isVerdict = ($key -eq 'Result' -or $key -eq 'Failures' -or
            $key -match 'Result$' -or
            $word -match '^(PASS|FAIL|EXACT|COMPLETE|SKIP|NO-|FAIL-|SKIP-)')
        if ($isVerdict -and -not $verdicts.Contains($key)) {
            $verdicts[$key] = $value
        }
    }
    return $verdicts
}

function Get-V9xVerdictWord {
    param([string]$Value)
    return ($Value -split '\s+')[0]
}

# ---------------------------------------------------------------- worker ---

if ($Worker) {
    # A v9xctl call that fails reports it in its result; the steps below check
    # what they need and throw themselves, as relcheck037.ps1 did.
    $ErrorActionPreference = "Continue"
    $machineSet = Import-V9xMachineList
    $entry = @($machineSet.Machines | Where-Object { $_.Name -eq $Worker })[0]
    if (-not $entry) {
        throw "No machine $Worker in $MachineList."
    }
    $controller = $machineSet.ControllerPath
    $package = Join-Path $repoRoot "build\win98se-$($entry.Family)"
    $machineDir = Join-Path $OutDir $(if ($Baseline) { "$Worker-before" } else { $Worker })
    New-Item -ItemType Directory -Force -Path $machineDir | Out-Null
    $connection = if ($entry.Relay) { @{ Relay = $entry.Relay } } else { @{ Host = $entry.Host } }
    $status = [ordered]@{
        Name = $Worker; Family = $entry.Family; Baseline = [bool]$Baseline
        BootBefore = $null; BootAfter = $null; Installed = $null; Error = $null
        Offline = $false; Agent = $null; Boot = $null; Desktop = $null; AnswerSeconds = $null
    }

    function Invoke-Ctl {
        param([string]$Verb, [hashtable]$Arguments = @{}, [int]$Timeout = 60)
        # v9xctl allows at most 120. A long exec or reset does not need more:
        # the verb's own TimeoutSeconds or wait governs it.
        $output = & $controller $Verb @Arguments @connection `
            -ConnectTimeoutSeconds ([Math]::Min($Timeout, 120)) -Json
        if (-not $output) {
            return $null
        }
        return ($output | Select-Object -Last 1) | ConvertFrom-Json
    }
    function Get-GuestFile {
        param([string]$From, [string]$To)
        & $controller get -Source $From -Destination $To @connection `
            -ConnectTimeoutSeconds 60 | Out-Null
    }
    function Send-GuestFile {
        param([string]$From, [string]$To)
        for ($try = 1; $try -le 2; ++$try) {
            & $controller put -Source $From -Destination $To @connection `
                -ConnectTimeoutSeconds 60 | Out-Null
            $hash = Invoke-Ctl hash @{ Path = $To; Source = $From }
            if ($hash -and $hash.Matches) {
                return
            }
            Write-Output "put $To attempt $try did not match"
        }
        throw "Could not put $To."
    }
    function Test-Installed {
        param([string]$Name)
        $hash = Invoke-Ctl hash @{ Path = "C:\WINDOWS\SYSTEM\$Name"; Source = (Join-Path $package $Name) }
        return ($hash -and $hash.Matches)
    }

    try {
        # One call first, so an offline machine costs one timeout. A relay
        # call to a machine that has stopped checking in waits at least 150 s
        # (v9xctl's relay open wait), and every later call would wait again:
        # with the puts' retries, over an hour for that worker, and the
        # parent waits for every worker.
        $asked = Get-Date
        $info = Invoke-Ctl info @{} 10
        if (-not $info -or -not $info.Success) {
            $status.Offline = $true
            throw "Not reachable."
        }
        $status.Agent = $info.AgentVersion
        $status.Boot = $info.BootCounter
        $status.Desktop = $info.DesktopReady
        $status.AnswerSeconds = [int]((Get-Date) - $asked).TotalSeconds
        Write-Output "agent $($info.AgentVersion), boot $($info.BootCounter), desktop $($info.DesktopReady)"
        if ($List) {
            exit 0
        }

        if (-not $Baseline) {
            $pending = Invoke-Ctl stat @{ Path = 'C:\WINDOWS\WININIT.INI' }
            if ($pending.Exists) {
                throw "A WININIT.INI is already pending; not installing over it."
            }
            $lines = @('[Rename]')
            foreach ($name in $installFiles.Keys) {
                Send-GuestFile (Join-Path $package $name) $installFiles[$name]
                $lines += "C:\WINDOWS\SYSTEM\$name=$($installFiles[$name])"
            }
            $wininit = Join-Path $machineDir 'WININIT.INI'
            [IO.File]::WriteAllText($wininit, (($lines -join "`r`n") + "`r`n"),
                [Text.Encoding]::ASCII)
            & $controller put -Source $wininit -Destination 'C:\WINDOWS\WININIT.INI' `
                @connection -ConnectTimeoutSeconds 60 | Out-Null
            $reset = Invoke-Ctl reset @{ WaitSeconds = 420 } 60
            $status.BootBefore = $reset.PreviousBootCounter
            $status.BootAfter = $reset.BootCounter
            Write-Output "reset: boot $($reset.PreviousBootCounter) -> $($reset.BootCounter)"
            # The i5-4570 takes over five minutes to boot.
            $null = Invoke-Ctl wait-desktop @{ WaitSeconds = 420 } 60

            $installed = 0
            foreach ($name in $installFiles.Keys) {
                $staged = Invoke-Ctl stat @{ Path = $installFiles[$name] }
                if ($staged.Exists) {
                    Write-Output "staged file still present: $($installFiles[$name])"
                    continue
                }
                if ((Test-Installed $name) -or (Test-Installed $name)) {
                    $installed++
                } else {
                    Write-Output "hash mismatch: $name"
                }
            }
            $status.Installed = $installed
            Write-Output "installed: $installed of $($installFiles.Count)"
            if ($installed -ne $installFiles.Count) {
                throw "The package is not installed as built."
            }
        } elseif (-not $info.DesktopReady) {
            # The probes need the desktop. A machine idle at its desktop
            # reports ready at once; one that is not (RIVA 128ZX on
            # 2026-11-06: a dialog, a locked screen) did not become ready in
            # 120 s either, so fail fast and say so.
            $desktop = Invoke-Ctl wait-desktop @{ WaitSeconds = 20 } 30
            if (-not $desktop -or -not $desktop.Success) {
                throw "Desktop not ready after 20 s; look at the machine."
            }
        }

        $null = Invoke-Ctl mkdir @{ Path = $guestDir }
        $probes = [ordered]@{
            'V9XD32.EXE'   = Join-Path $repoRoot 'build\d3d32-probe\V9XD32.EXE'
            'V9XGLP.EXE'   = Join-Path $repoRoot 'build\gl-probe\V9XGLP.EXE'
            'V9XGLIDP.EXE' = Join-Path $repoRoot 'build\glide-probe\V9XGLIDP.EXE'
            'V9XR3DP.EXE'  = Join-Path $repoRoot 'build\r3d-probe\V9XR3DP.EXE'
            'V9XTRACE.EXE' = Join-Path $package 'V9XTRACE.EXE'
        }
        foreach ($name in $probes.Keys) {
            Send-GuestFile $probes[$name] "$guestDir\$name"
        }

        # The agent cannot delete and its stat has no time, so each report is
        # overwritten with this before its probe runs: a probe that never
        # started leaves no Result instead of an earlier run's verdicts. It
        # holds no key, because V9XGLIDP updates its own INI section through
        # WritePrivateProfileString and a key outside it would survive.
        $notRun = [IO.Path]::GetTempFileName()
        [IO.File]::WriteAllText($notRun, "; placeholder from run-release-checks.ps1`r`n",
            [Text.Encoding]::ASCII)

        foreach ($run in @($entry.D32Runs)) {
            $tag = if ($run) { $run.TrimStart('/') } else { 'default' }
            Send-GuestFile $notRun 'C:\V9XDIAG\V9XD32.TXT'
            $result = Invoke-Ctl exec @{
                Application = "$guestDir\V9XD32.EXE"; Arguments = $run
                WorkingDirectory = $guestDir; TimeoutSeconds = 120
            } 120
            Get-GuestFile 'C:\V9XDIAG\V9XD32.TXT' (Join-Path $machineDir "v9xd32-$tag-V9XD32.TXT")
            Write-Output "V9XD32 $tag exit=$($result.ExitCode)"
        }
        foreach ($probe in $probeReports) {
            Send-GuestFile $notRun "C:\V9XDIAG\$($probe.Report)"
            $result = Invoke-Ctl exec @{
                Application = "$guestDir\$($probe.Exe)"
                WorkingDirectory = $guestDir; TimeoutSeconds = 120
            } 120
            Get-GuestFile "C:\V9XDIAG\$($probe.Report)" `
                (Join-Path $machineDir "$($probe.Prefix)-$($probe.Report)")
            Write-Output "$($probe.Exe) exit=$($result.ExitCode)"
        }

        if ($ThreeDMark -and $entry.ThreeDMark) {
            $grabDir = Join-Path $machineDir '3dmark2000-grab'
            New-Item -ItemType Directory -Force -Path $grabDir | Out-Null
            $traceArgs = @{ Application = "$guestDir\V9XTRACE.EXE"; WorkingDirectory = $guestDir; TimeoutSeconds = 120 }
            $null = Invoke-Ctl exec $traceArgs 150
            $null = Invoke-Ctl exec ($traceArgs + @{ Arguments = '-grab:40x10' }) 150
            $null = Invoke-Ctl exec @{
                Application = 'C:\Program Files\MadOnion.com\3DMark2000\3DMark2000.exe'
                WorkingDirectory = 'C:\Program Files\MadOnion.com\3DMark2000'; Detach = $true
            }
            $null = Invoke-Ctl wait-window @{ Title = '3DMark2000'; TimeoutSeconds = 120 } 150
            $null = Invoke-Ctl input @{ Sequence = $entry.ThreeDMark }
            $finished = $false
            for ($i = 0; $i -lt 12 -and -not $finished; ++$i) {
                $null = Invoke-Ctl wait-window @{ Title = 'Overall Score'; TimeoutSeconds = 120 } 150
                $titles = @((Invoke-Ctl windows).Windows | ForEach-Object { $_.Title }) -join ' | '
                $finished = ($titles -match 'Overall Score')
            }
            Write-Output "3DMark finished: $finished"
            & $controller screenshot -OutFile (Join-Path $machineDir '3dmark-score.png') `
                @connection -ConnectTimeoutSeconds 120 | Out-Null
            $null = Invoke-Ctl kill @{ Name = '3DMARK2000.EXE' }
            $null = Invoke-Ctl exec $traceArgs 150
            for ($i = 0; $i -lt 10; ++$i) {
                Get-GuestFile "C:\V9XDIAG\V9XGRAB$i.BMP" (Join-Path $grabDir "V9XGRAB$i.BMP")
            }
            Get-GuestFile 'C:\V9XDIAG\V9XSNA7.INI' (Join-Path $grabDir 'V9XSNA7.INI')
        }
    }
    catch {
        $status.Error = $_.Exception.Message
        Write-Output "ERROR: $($status.Error)"
    }
    finally {
        $status | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $machineDir 'status.json') -Encoding Ascii
    }
    if ($status.Error) {
        exit 1
    }
    exit 0
}

# ---------------------------------------------------------------- parent ---

$machineSet = Import-V9xMachineList
if (-not (Test-Path -LiteralPath $machineSet.ControllerPath)) {
    throw "ControllerPath in $MachineList does not exist: $($machineSet.ControllerPath)"
}
$machines = @($machineSet.Machines | Where-Object { $_.Enabled -ne $false })
if ($Machine) {
    $unknown = @($Machine | Where-Object { $_ -notin @($machineSet.Machines | ForEach-Object { $_.Name }) })
    if ($unknown.Count -ne 0) {
        throw "Unknown machine(s): $($unknown -join ', ')."
    }
    $machines = @($machineSet.Machines | Where-Object { $_.Name -in $Machine })
}
if ($List -and -not $Machine) {
    $machines = @($machineSet.Machines)
}
if ($machines.Count -eq 0) {
    throw "No machines to run."
}

if ($List) {
    # Nothing is kept from a listing; the workers' status files go to a
    # temporary folder.
    $OutDir = Join-Path ([IO.Path]::GetTempPath()) ("v9x-release-list-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $SummaryOnly = $false
} else {
$indexPath = Join-Path $repoRoot "build\packages.json"
if (-not (Test-Path -LiteralPath $indexPath)) {
    throw "No build\packages.json. Run scripts\build-all-packages.ps1 first."
}
$index = Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json
$buildId = $index.BuildId
foreach ($entry in $machines) {
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot "build\win98se-$($entry.Family)"))) {
        throw "No package for family $($entry.Family) (machine $($entry.Name))."
    }
}

if (-not $OutDir) {
    $OutDir = Join-Path $repoRoot ("docs\probe\{0}-release-checks-{1}" -f
        $index.Version, (Get-Date -Format 'yyyy-MM-dd'))
}
}
$OutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

if ($SummaryOnly) {
    $processes = @()
} elseif (-not $SkipProbeBuild -and -not $List) {
    Write-Output "Building the probes with build id $buildId"
    & (Join-Path $PSScriptRoot "build-d3d32-probe.ps1") -BuildId $buildId | Out-Null
    & (Join-Path $PSScriptRoot "build-gl-probe.ps1") -BuildId $buildId | Out-Null
    & (Join-Path $PSScriptRoot "build-glide-probe.ps1") -BuildId $buildId | Out-Null
    & (Join-Path $PSScriptRoot "build-r3d-probe.ps1") -BuildId $buildId | Out-Null
}

$powershell = (Get-Command 'powershell.exe').Source
$started = Get-Date
if (-not $SummaryOnly) {
$processes = @()
foreach ($entry in $machines) {
    $log = Join-Path $OutDir "$($entry.Name)$(if ($Baseline) { '-before' }).log"
    $arguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"",
        '-Worker', $entry.Name, '-MachineList', "`"$MachineList`"",
        '-OutDir', "`"$OutDir`""
    )
    if ($Baseline) { $arguments += '-Baseline' }
    if ($ThreeDMark) { $arguments += '-ThreeDMark' }
    if ($List) { $arguments += '-List' }
    $process = Start-Process -FilePath $powershell -ArgumentList $arguments `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err" `
        -WindowStyle Hidden -PassThru
    # Windows PowerShell 5.1 reports no ExitCode for a Start-Process child
    # unless its handle was opened while it ran.
    $null = $process.Handle
    $processes += [pscustomobject]@{
        Name = $entry.Name; Family = $entry.Family; Process = $process; Log = $log
        Folder = $(if ($Baseline) { "$($entry.Name)-before" } else { $entry.Name })
        Answered = $false; Skipped = $false
    }
    if (-not $List) {
        Write-Output "Started $($entry.Name) ($($entry.Family)): $log"
    }
}
# Report each machine as it finishes, not in start order, and skip any that
# has not answered within ReachSeconds: its worker prints "agent ..." after
# its first call succeeds.
function Read-V9xNewLines {
    param([string]$Path, [int]$Skip)
    if (-not (Test-Path -LiteralPath $Path)) {
        return @()
    }
    # The worker still holds the file open for writing.
    $stream = [IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
    try {
        $text = (New-Object IO.StreamReader($stream)).ReadToEnd()
    }
    finally {
        $stream.Dispose()
    }
    $lines = @($text -split "`r?`n" | Where-Object { $_.Trim() })
    return @($lines | Select-Object -Skip $Skip)
}

# One line per machine when it finishes: how long, how many probe runs
# exited 0, and the error that stopped it, if any. The step-by-step detail
# stays in <name>.log.
function Write-V9xFinished {
    param($Worker)
    $lines = @(Read-V9xNewLines -Path $Worker.Log -Skip 0)
    $runs = @($lines | Where-Object { $_ -match ' exit=-?\d+$' })
    $clean = @($runs | Where-Object { $_ -match ' exit=0$' })
    $errorLine = @($lines | Where-Object { $_ -like 'ERROR: *' }) | Select-Object -First 1
    $text = "{0} finished in {1:N1} min: {2} of {3} probe runs exit 0" -f $Worker.Name,
        ((Get-Date) - $started).TotalMinutes, $clean.Count, $runs.Count
    if ($errorLine) {
        $text += "; $errorLine"
    }
    Write-Output $text
}

$running = @($processes)
while ($running.Count -gt 0) {
    Start-Sleep -Seconds 1
    $late = ((Get-Date) - $started).TotalSeconds -gt $ReachSeconds
    foreach ($p in $running) {
        if (-not $late -or $p.Answered -or $p.Process.HasExited) {
            continue
        }
        if (Select-String -LiteralPath $p.Log -Pattern '^agent ' -Quiet) {
            $p.Answered = $true
            continue
        }
        Stop-Process -Id $p.Process.Id -Force -ErrorAction SilentlyContinue
        $p.Process.WaitForExit()
        $p.Skipped = $true
        $statusDir = Join-Path $OutDir $p.Folder
        New-Item -ItemType Directory -Force -Path $statusDir | Out-Null
        [ordered]@{
            Name = $p.Name; Family = $p.Family; Baseline = [bool]$Baseline
            BootBefore = $null; BootAfter = $null; Installed = $null
            Error = "skipped: no answer within $ReachSeconds s"; Offline = $true; Agent = $null
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $statusDir 'status.json') -Encoding Ascii
        if (-not $List) {
            Write-Output "$($p.Name) skipped: no answer within $ReachSeconds s"
        }
    }
    $done = @($running | Where-Object { $_.Process.HasExited })
    foreach ($p in @($done | Where-Object { -not $_.Skipped -and -not $List })) {
        Write-V9xFinished $p
    }
    $running = @($running | Where-Object { $_ -notin $done })
}
}

if ($List) {
    $rows = foreach ($entry in $machines) {
        $statusPath = Join-Path $OutDir "$($entry.Name)\status.json"
        $s = if (Test-Path -LiteralPath $statusPath) {
            Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json
        }
        $answered = ($s -and -not $s.Offline)
        [pscustomobject]@{
            Machine = $entry.Name
            Family  = $entry.Family
            Address = $(if ($entry.Relay) { $entry.Relay } else { $entry.Host })
            Online  = $(if ($answered) { "yes, $($s.AnswerSeconds) s" } else { 'no' })
            Agent   = $(if ($answered) { $s.Agent } else { '' })
            Boot    = $(if ($answered) { $s.Boot } else { '' })
            Desktop = $(if ($answered) { $(if ($s.Desktop) { 'ready' } else { 'NOT READY' }) } else { '' })
            InList  = $(if ($entry.Enabled -eq $false) { 'off' } else { 'on' })
        }
    }
    $rows | Sort-Object { $_.Online -eq 'no' }, Machine | Format-Table -AutoSize | Out-String -Width 200
    $ready = @($rows | Where-Object { $_.Desktop -eq 'ready' } | ForEach-Object { $_.Machine })
    Write-Output "Ready: -Machine $($ready -join ',')"
    Remove-Item -LiteralPath $OutDir -Recurse -Force -ErrorAction SilentlyContinue
    exit 0
}

# ---------------------------------------------------------------- summary ---

# The check a report file is, from the names the worker gives them.
function Get-V9xCheckName {
    param([string]$FileName)
    if ($FileName -match '^v9xd32-(.+)-V9XD32\.TXT$') {
        return "V9XD32 $($Matches[1])"
    }
    if ($FileName -match '^[a-z0-9]+-(V9X[A-Z0-9]+)\.INI$') {
        return $Matches[1]
    }
    return $null
}

function Get-V9xMachineReports {
    param([string]$Dir)
    $reports = [ordered]@{}
    if (-not (Test-Path -LiteralPath $Dir)) {
        return $reports
    }
    foreach ($file in @(Get-ChildItem -LiteralPath $Dir -File | Sort-Object Name)) {
        $check = Get-V9xCheckName $file.Name
        if ($check) {
            $reports[$check] = $file.FullName
        }
    }
    return $reports
}

# One cell: the Result, and for a failing run the failing tests by name.
function Get-V9xCell {
    param([string]$Path)
    $verdicts = Get-V9xProbeVerdicts -Path $Path
    $result = if ($verdicts.Contains('Result')) { $verdicts['Result'] } else { 'NOT-RUN (no report)' }
    $failed = @($verdicts.Keys | Where-Object {
        $_ -ne 'Result' -and (Get-V9xVerdictWord $verdicts[$_]) -match '^FAIL' })
    $failed = @($failed | ForEach-Object { $_ -replace 'Result$', '' })
    if ($failed.Count -gt 0) {
        $shown = @($failed | Select-Object -First 6)
        $more = if ($failed.Count -gt 6) { ", +$($failed.Count - 6)" } else { '' }
        $result += ": $($shown -join ', ')$more"
    } elseif ($verdicts.Contains('Failures') -and $verdicts['Failures'] -notmatch '^0x0+$') {
        # Some V9XD32 tests (T21) fail only through the Failures mask.
        $result += " (Failures=$($verdicts['Failures']))"
    }
    $build = Select-String -LiteralPath $Path -Pattern '^Build=(.*)$' | Select-Object -First 1
    if ($build -and $build.Matches[0].Groups[1].Value -ne $buildId) {
        $result += " (Build=$($build.Matches[0].Groups[1].Value))"
    }
    return ($result -replace '\|', '/')
}

if (-not $Previous) {
    $Previous = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'docs\probe') -Directory |
        Where-Object { $_.Name -match '-release-checks' -and $_.FullName -ne $OutDir } |
        Sort-Object Name | Select-Object -Last 1 | ForEach-Object { $_.FullName })[0]
}

$folderNames = @($machines | ForEach-Object { if ($Baseline) { "$($_.Name)-before" } else { $_.Name } })
$columns = @()
$allChecks = New-Object System.Collections.Generic.List[string]
foreach ($folder in $folderNames) {
    $reports = Get-V9xMachineReports (Join-Path $OutDir $folder)
    $columns += [pscustomobject]@{ Folder = $folder; Reports = $reports }
    foreach ($check in $reports.Keys) {
        if (-not $allChecks.Contains($check)) { $allChecks.Add($check) }
    }
}

$md = @()
$md += "# Release checks, $($index.Version), build $buildId"
$md += ''
$md += "Generated by scripts/run-release-checks.ps1 on $(Get-Date -Format 'yyyy-MM-dd HH:mm')."
if ($buildId -match '-dirty$') {
    $md += ''
    $md += "The packages were built from a dirty tree ($buildId): a measurement, not a release check."
}
$md += ''
$md += '## Installs'
$md += ''
$md += '| Machine | Family | Boot | Installed | Error |'
$md += '|---|---|---|---|---|'
foreach ($folder in $folderNames) {
    $statusPath = Join-Path $OutDir "$folder\status.json"
    if (-not (Test-Path -LiteralPath $statusPath)) {
        $md += "| $folder | | | | no status.json (see $folder.log) |"
        continue
    }
    $s = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json
    $boot = if ($s.BootBefore -ne $null) { "$($s.BootBefore) to $($s.BootAfter)" } else { '' }
    $installed = if ($s.Baseline) { 'as found' } elseif ($s.Installed -ne $null) { "$($s.Installed) of $($installFiles.Count)" } else { '' }
    $md += "| $folder | $($s.Family) | $boot | $installed | $($s.Error) |"
}
$md += ''
$md += '## Results'
$md += ''
$md += "| Check | $(($folderNames) -join ' | ') |"
$md += "|---|$(($folderNames | ForEach-Object { '---' }) -join '|')|"
foreach ($check in $allChecks) {
    $cells = @($columns | ForEach-Object {
        if ($_.Reports.Contains($check)) { Get-V9xCell $_.Reports[$check] } else { '' }
    })
    $md += "| $check | $($cells -join ' | ') |"
}

$md += ''
$md += '## Changes against the previous checks'
$md += ''
if (-not $Previous) {
    $md += 'No earlier release-checks folder to compare with.'
} else {
    $md += "Compared with ``$(Split-Path -Leaf $Previous)``, by machine name and check. Only verdicts that differ are listed."
    $md += ''
    foreach ($column in $columns) {
        $previousDir = Join-Path $Previous $column.Folder
        if (-not (Test-Path -LiteralPath $previousDir) -and $column.Folder -match '-before$') {
            # A baseline compares with the driver the last release checked.
            $previousDir = Join-Path $Previous ($column.Folder -replace '-before$', '')
        }
        if (-not (Test-Path -LiteralPath $previousDir)) {
            $md += "- **$($column.Folder)**: not in the previous checks."
            continue
        }
        $old = Get-V9xMachineReports $previousDir
        $changes = @()
        foreach ($check in $column.Reports.Keys) {
            if (-not $old.Contains($check)) {
                $changes += "$check is new"
                continue
            }
            $a = Get-V9xProbeVerdicts -Path $old[$check]
            $b = Get-V9xProbeVerdicts -Path $column.Reports[$check]
            foreach ($key in @(@($a.Keys) + @($b.Keys) | Select-Object -Unique)) {
                $was = if ($a.Contains($key)) { Get-V9xVerdictWord $a[$key] } else { '(absent)' }
                $now = if ($b.Contains($key)) { Get-V9xVerdictWord $b[$key] } else { '(absent)' }
                if ($key -eq 'Failures') {
                    $was = if ($a.Contains($key)) { $a[$key] } else { '(absent)' }
                    $now = if ($b.Contains($key)) { $b[$key] } else { '(absent)' }
                }
                if ($was -ne $now) {
                    $changes += "$check $key`: $was -> $now"
                }
            }
        }
        foreach ($check in $old.Keys) {
            if (-not $column.Reports.Contains($check)) {
                $changes += "$check not run this time"
            }
        }
        if ($changes.Count -eq 0) {
            $md += "- **$($column.Folder)**: no verdict changed."
        } else {
            $md += "- **$($column.Folder)**:"
            $md += @($changes | ForEach-Object { "  - $_" })
        }
    }
}

$summaryPath = Join-Path $OutDir 'SUMMARY.md'
[IO.File]::WriteAllText($summaryPath, (($md -join "`r`n") + "`r`n"),
    (New-Object Text.UTF8Encoding $false))
Write-Output "Wrote $summaryPath"
$skipped = @($processes | Where-Object { $_.Skipped })
if ($skipped.Count -ne 0) {
    Write-Output "Skipped, no answer: $(@($skipped | ForEach-Object { $_.Name }) -join ', ')"
}
$failed = @($processes | Where-Object { -not $_.Skipped -and $_.Process.ExitCode -ne 0 })
if ($failed.Count -ne 0) {
    Write-Output "Machines that did not finish: $(@($failed | ForEach-Object { $_.Name }) -join ', ')"
    exit 1
}

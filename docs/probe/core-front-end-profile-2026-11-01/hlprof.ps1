param([Parameter(Mandatory = $true)][string]$Tag,
      [ValidateSet('d3d', 'gl')][string]$Api = 'd3d',
      [string]$OutDir = 'C:\everything\velocity9x\docs\probe\gen3-batch-cost-step0-2026-11-01\half-life',
      [switch]$Profiler)
# Half-Life mwd5 on the netbook, full screen, as the 2026-10-01 profile ran
# it: one warm-up timedemo, then a V9XTRACE snapshot immediately before and
# after a second, so the window is (nearly) the demo alone.
$ErrorActionPreference = 'Continue'
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
$log = 'C:\Sierra\Half-Life\valve\qconsole.log'
$trace = 'C:\V9XREL\V9XTRACE.EXE'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
function Snap([string]$name) {
    $r = J exec -Application $trace -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
    "snap $name exit=$($r.ExitCode)"
    & $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $OutDir "$Tag-$name-V9XSNA7.INI") | Out-Null
}
function Demo([string]$label) {
    $null = J input -Sequence "type echo V9X_${Tag}_$label; key ENTER"
    $null = J wait-file -Path $log -Match "V9X_${Tag}_$label" -WaitSeconds 20
    if ($LASTEXITCODE -ne 0) { throw "console did not take input ($label)" }
    $null = J input -Sequence 'type timedemo mwd5; key ENTER'
    # Not -Until: it accepts the last matching line already in the log,
    # which is the previous demo's. A follow prints only new lines; the
    # first fps line among them ends it.
    # v9xctl writes followed lines straight to its console, past the
    # PowerShell pipeline, so it runs as a child whose output is read and
    # which is stopped at the first line.
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'powershell.exe'
    $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$c`" cat -EndpointHost 10.0.1.6 -Port 9869 -Path `"$log`" -Follow -Match fps -WaitSeconds 300"
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.CreateNoWindow = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $line = $null
    while ($null -ne ($l = $p.StandardOutput.ReadLine())) {
        if ($l -match 'fps') { $line = $l; break }
    }
    if (-not $p.HasExited) { $p.Kill() }
    "$(Get-Date -Format HH:mm:ss) $label : $line"
}

$reg = if ($Api -eq 'gl') { 'C:\V9XDIAG\HLGL.REG' } else { 'C:\V9XDIAG\HLD3D.REG' }
$null = J exec -Application 'C:\WINDOWS\REGEDIT.EXE' -Arguments "/s $reg" -WaitForExit -WaitSeconds 60
$hlArgs = '-console -condebug' + $(if ($Api -eq 'gl') { ' -gl' } else { '' })
# The old session's log stays until the new one writes, and the agent has
# no delete: a placeholder over it makes "this session" detectable.
$placeholder = Join-Path $env:TEMP 'v9x-qc-placeholder.log'
Set-Content -LiteralPath $placeholder -Value 'V9X placeholder' -Encoding ascii
& $c put @conn -Source $placeholder -Destination $log | Out-Null
if ($Profiler) {
    # V9XPROF starts Half-Life itself, from Half-Life's folder, and writes
    # its samples once Half-Life exits.
    $null = J exec -Application 'C:\V9XREL\V9XPROF.EXE' -Arguments "C:\Sierra\Half-Life\hl.exe $hlArgs" -WorkingDirectory 'C:\V9XREL' -Detach
} else {
    $null = J exec -Application 'C:\Sierra\Half-Life\hl.exe' -Arguments $hlArgs -WorkingDirectory 'C:\Sierra\Half-Life' -Detach
}
# The log's size does not always show Half-Life's startup lines, so the
# menu is found by trying: open the console and check a tag lands.
$null = J wait-window -Title 'Half-Life' -WaitSeconds 240
$ready = $false
for ($try = 1; $try -le 12 -and -not $ready; ++$try) {
    $null = J input -Sequence "move 103,192; click left; delay 2000; type echo V9X_${Tag}_READY$try; key ENTER"
    $null = J wait-file -Path $log -Match "V9X_${Tag}_READY$try" -WaitSeconds 15
    $ready = ($LASTEXITCODE -eq 0)
}
"console ready=$ready after $try tries"
if (-not $ready) { throw 'Half-Life console never answered' }
Demo 'WARM'
Snap 'pre'
Demo 'RUN'
Snap 'post'
$null = J input -Sequence 'type quit; key ENTER'
$null = J wait-process -Name 'HL.EXE' -WaitSeconds 90
if ($LASTEXITCODE -ne 0) {
    $null = J kill -Name 'HL.EXE'
    $null = J wait-process -Name 'HL.EXE' -WaitSeconds 60
    "HL.EXE killed; wait exit=$LASTEXITCODE"
}
if ($Profiler) {
    $null = J wait-process -Name 'V9XPROF.EXE' -WaitSeconds 120
    "V9XPROF wait exit=$LASTEXITCODE"
    & $c get @conn -Source 'C:\V9XDIAG\V9XPROF.TXT' -Destination (Join-Path $OutDir "$Tag-V9XPROF.TXT") | Out-Null
    & $c get @conn -Source 'C:\V9XDIAG\V9XPROF.BIN' -Destination (Join-Path $OutDir "$Tag-V9XPROF.BIN") | Out-Null
}
& $c get @conn -Source $log -Destination (Join-Path $OutDir "$Tag-qconsole.log") | Out-Null
Select-String -LiteralPath (Join-Path $OutDir "$Tag-qconsole.log") -Pattern 'V9X_|fps|Direct3D|OpenGL' | ForEach-Object Line

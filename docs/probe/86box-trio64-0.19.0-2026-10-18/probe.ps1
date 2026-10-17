param([string]$Tag, [string]$PackageLocal, [string]$PackageRemote)
# One pass on the Trio64 VM: installed files against the package, then
# V9XDDP, V9XGDI and a V9XMSW mode cycle, each result fetched to $Tag.
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$ep = @{ EndpointHost = '127.0.0.1'; Port = 9871 }
$d = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) $Tag
New-Item -ItemType Directory -Force $d | Out-Null

foreach ($f in 'V9XDISP.DRV', 'V9XMINI.VXD', 'V9XHAL.DLL', 'V9XGL.DLL', 'V9XSETP.DLL') {
    $null = & $ctl get @ep -Source "C:\WINDOWS\SYSTEM\$f" -Destination "$d\$f" -Json
    $a = if (Test-Path "$d\$f") { (Get-FileHash "$d\$f").Hash } else { 'MISSING' }
    $p = Join-Path $PackageLocal $f
    $b = if (Test-Path $p) { (Get-FileHash $p).Hash } else { 'NOT-IN-PACKAGE' }
    '{0,-12} {1}' -f $f, $(if ($a -eq $b) { 'matches package' } else { "DIFFERS ($b)" })
}

$runs = @(
    @('V9XDDP.EXE', '', @('V9XDD.INI', 'V9XDD2.INI')),
    @('V9XGDI.EXE', '', @('V9XGDI.INI')),
    @('V9XMSW.EXE', '/cycle:3', @('V9XMSW.INI'))
)
foreach ($run in $runs) {
    $x = & $ctl exec @ep -Application "$PackageRemote\$($run[0])" -Arguments $run[1] -WorkingDirectory $PackageRemote -TimeoutSeconds 300 -Json | ConvertFrom-Json
    Start-Sleep -Seconds 3
    foreach ($ini in $run[2]) {
        $null = & $ctl get @ep -Source "C:\V9XDIAG\$ini" -Destination "$d\$ini" -Json 2>$null
    }
    $first = Join-Path $d $run[2][0]
    $ok1 = 0; $ok0 = 0; $result = ''
    foreach ($ini in $run[2]) {
        $p = Join-Path $d $ini
        if (Test-Path $p) {
            $ok1 += (Select-String -Path $p -Pattern '(Ok=1|=PASS)').Count
            $ok0 += (Select-String -Path $p -Pattern '(Ok=0|=FAIL)').Count
        }
    }
    if (Test-Path $first) {
        $result = (Select-String -Path $first -Pattern '^(Result|Overall|Status)=' | Select-Object -First 1).Line
    }
    '{0} exit={1} timedout={2} pass={3} fail={4} {5}' -f $run[0], $x.ExitCode, $x.TimedOut, $ok1, $ok0, $result
}

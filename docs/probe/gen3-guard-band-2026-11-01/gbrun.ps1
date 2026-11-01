param([Parameter(Mandatory = $true)][int]$Max,
      [string]$OutDir = 'C:\everything\velocity9x\docs\probe\gen3-guard-band-2026-11-01')
# Run V9XGBND on the netbook up to distance $Max and fetch its INI and the
# PPM of every unclipped draw.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
$out = Join-Path $OutDir "max-$Max"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$r = J put -Source 'C:\everything\velocity9x\build\guard-band-probe\V9XGBND.EXE' -Destination 'C:\V9XREL\V9XGBND.EXE'
"put ok=$($r.Success)"
$r = J exec -Application 'C:\V9XREL\V9XGBND.EXE' -Arguments "-max:$Max" -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 300
"exec exit=$($r.ExitCode) agent=$LASTEXITCODE"
& $c get @conn -Source 'C:\V9XDIAG\V9XGBND.INI' -Destination (Join-Path $out 'V9XGBND.INI') | Out-Null
Get-Content (Join-Path $out 'V9XGBND.INI')
$list = J ls -RemotePath 'C:\V9XDIAG'
foreach ($e in $list.Entries) {
    if ($e.Name -like 'GB*.PPM') {
        & $c get @conn -Source "C:\V9XDIAG\$($e.Name)" -Destination (Join-Path $out $e.Name) | Out-Null
    }
}
$info = J info
"boot=$($info.BootCounter) uptime=$($info.UptimeMilliseconds)"

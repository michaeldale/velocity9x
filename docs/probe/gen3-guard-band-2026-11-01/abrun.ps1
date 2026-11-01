param([Parameter(Mandatory = $true)][string]$Tag,
      [switch]$Guard,
      [switch]$NoReset,
      [string]$Root = 'C:\everything\velocity9x\docs\probe\gen3-guard-band-2026-11-01\step2-ab')
# One arm of the guard-band A/B: reset the netbook, set the mask (the band
# at 1,024, or zero), run Half-Life D3D then 3DMark 2000, and record the
# visible windows after each so a fault dialog is seen, not inferred.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
$s = 'C:\Users\michael\AppData\Local\Temp\claude\C--everything-velocity9x\4be3f200-39f9-44aa-8aa4-c90363b7ee94\scratchpad'
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
function Windows([string]$label) {
    $w = J windows
    $titles = @($w.Windows | ForEach-Object { "$($_.Title) [$($_.Class)]" }) -join ' | '
    "$label windows: $titles"
    Add-Content -LiteralPath (Join-Path $out 'windows.txt') -Value "$label : $titles"
}
$out = Join-Path $Root $Tag
New-Item -ItemType Directory -Force -Path $out | Out-Null
if (-not $NoReset) {
    $rs = J reset -JobId "gb-$Tag" -WaitSeconds 600
    "reset: boot $($rs.PreviousBootCounter) -> $($rs.BootCounter) desktop=$($rs.DesktopReady)"
    $null = J wait-desktop -WaitSeconds 300
    # The wifi utility puts up its window a while after boot and takes the
    # focus; a full-screen game that loses it stalls (a1-mask0, boot 264).
    $null = J wait-window -Title 'NETGEAR' -WaitSeconds 180
    $null = J input -Sequence 'delay 5000'
}
$null = J kill -Name 'HL.EXE'
$null = J wait-process -Name 'HL.EXE' -WaitSeconds 60
$mask = if ($Guard) { 0x100 -bor (1024 -shl 16) } else { 0 }
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -Arguments "-omit:$mask" -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
& $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $out 'arm-V9XSNA7.INI') | Out-Null
(Select-String -LiteralPath (Join-Path $out 'arm-V9XSNA7.INI') -Pattern '^(Omit(Requested|Accepted)|BootId|HalBuild)=' | ForEach-Object Line) -join ' '
Windows 'start'
& (Join-Path $s 'hlprof.ps1') -Tag $Tag -Api d3d -OutDir (Join-Path $out 'half-life') | Select-String -Pattern 'WARM|RUN :|ready|killed'
Windows 'after-hl'
& (Join-Path $s 'mark2000.ps1') -OutDir (Join-Path $out '3dmark2000') 2>&1 | Select-String -Pattern 'windows'
Windows 'after-3dmark'
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -Arguments '-omit:0' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
& $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $out 'end-V9XSNA7.INI') | Out-Null
"$Tag done; mask 0"

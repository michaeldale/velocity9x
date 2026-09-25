param([string]$Before, [string]$After)
# Per-bucket HAL time between two V9XTRACE snapshots, in milliseconds,
# using the TSC/tick calibration the HAL records at each flip.
function Read-Ini([string]$Path) {
    $h = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^([A-Za-z0-9_]+)=(.*)$') { $h[$Matches[1]] = $Matches[2].Trim() }
    }
    return $h
}
function Num([hashtable]$h, [string]$k) {
    if (-not $h.ContainsKey($k)) { return [double]0 }
    $v = $h[$k]
    if ($v -like '0x*') { return [double][Convert]::ToUInt32($v.Substring(2), 16) }
    return [double]$v
}
function Pair([hashtable]$h, [string]$lo, [string]$hi) {
    return (Num $h $hi) * 4294967296.0 + (Num $h $lo)
}
$a = Read-Ini $Before
$b = Read-Ini $After
$tscSpan = (Pair $b 'TimeTscLastLo' 'TimeTscLastHi') - (Pair $b 'TimeTscFirstLo' 'TimeTscFirstHi')
$tickSpan = (Num $b 'TimeTickLast') - (Num $b 'TimeTickFirst')
if ($tickSpan -le 0) { throw 'no calibration span' }
$perMs = $tscSpan / $tickSpan
$wallMs = (Num $b 'DumpUptimeMs') - (Num $a 'DumpUptimeMs')
'TSC {0:N0} cycles/ms ({1:N0} MHz) over {2:N0} ms of flips; snapshot interval {3:N0} ms' -f $perMs, ($perMs / 1000), $tickSpan, $wallMs
'{0,-14} {1,12} {2,10} {3,12} {4,8}' -f 'bucket', 'calls', 'ms', 'us/call', '% wall'
$names = 'D3dCalls','EngineDraw','Decode','RingWrite','HeadWait','CrumbWait','Flip','Lock','BltCopy','BltFill','CreateSurface','LockHeld'
foreach ($n in $names) {
    $cyc = (Pair $b "Time${n}CyclesLo" "Time${n}CyclesHi") - (Pair $a "Time${n}CyclesLo" "Time${n}CyclesHi")
    $calls = (Num $b "Time${n}Calls") - (Num $a "Time${n}Calls")
    $ms = $cyc / $perMs
    $us = if ($calls -gt 0) { $ms * 1000 / $calls } else { 0 }
    '{0,-14} {1,12:N0} {2,10:N0} {3,12:N1} {4,8:N1}' -f $n, $calls, $ms, $us, (100 * $ms / $wallMs)
}

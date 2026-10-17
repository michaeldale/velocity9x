param([string]$Before, [string]$After, [double]$Ghz = 3.19)
# Per-bucket deltas between two V9XTRACE snapshots: calls, ms, microseconds a call.
function Read-Keys([string]$path) {
    $keys = @{}
    foreach ($line in Get-Content $path) {
        if ($line -match '^([^=]+)=(.*)$') { $keys[$matches[1]] = $matches[2] }
    }
    return $keys
}
function Num($keys, [string]$name) {
    $value = $keys[$name]
    if ($value -eq $null) { return [double]0 }
    if ($value -like '0x*') { return [double][Convert]::ToUInt64($value.Substring(2), 16) }
    return [double]$value
}
$b = Read-Keys $Before
$a = Read-Keys $After
$names = 'D3dCalls','EngineDraw','RenderDrain','RingWait','Decode','RingWrite','Flip','Lock','LockHeld','BltCopy','BltFill','CreateSurface'
foreach ($n in $names) {
    $cycles = ((Num $a "Time${n}CyclesHi") * 4294967296 + (Num $a "Time${n}CyclesLo")) -
              ((Num $b "Time${n}CyclesHi") * 4294967296 + (Num $b "Time${n}CyclesLo"))
    $calls = (Num $a "Time${n}Calls") - (Num $b "Time${n}Calls")
    $ms = $cycles / ($Ghz * 1e6)
    $per = if ($calls -gt 0) { ($cycles / $calls) / ($Ghz * 1e3) } else { 0 }
    '{0,-14} calls={1,10:N0} ms={2,10:N1} us/call={3,8:N2}' -f $n, $calls, $ms, $per
}
foreach ($k in 'R3dListSinkBatches','R3dListTrianglesIn','D3dRenderPrimitiveCalls') {
    '{0} delta={1:N0}' -f $k, ((Num $a $k) - (Num $b $k))
}

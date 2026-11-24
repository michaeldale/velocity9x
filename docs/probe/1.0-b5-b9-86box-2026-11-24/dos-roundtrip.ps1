param(
    [Parameter(Mandatory)][int]$ProcessId,   # the clone's 86Box process
    [Parameter(Mandatory)][string]$Out,
    [int]$Port = 9879
)
# B5: a full-screen DOS box round trip, as in the 2026-11-10 record:
# Start > Run `command`, `echo AAA` windowed, Alt+Enter, `echo BBB` in
# full screen (ended by CTRL+M: the agent's RETURN does not reach a
# full-screen VM), Alt+Enter back, `echo CCC`, `exit`. Host-side
# PrintWindow capture of the 86Box window and an agent ping after each
# step; the agent's own screenshot only after the return.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$cap = Join-Path $PSScriptRoot 'capture-pid.ps1'
New-Item -ItemType Directory -Force $Out | Out-Null
function Send([string]$sequence) {
    & $c -Port $Port input -Sequence $sequence -Json 2>&1 | Out-String | Out-Null
}
function Mark([string]$name) {
    Start-Sleep -Seconds 3
    & $cap -ProcessId $ProcessId -OutFile (Join-Path $Out "$name.png") | Out-Null
    $r = & $c -Port $Port info -Json 2>&1 | Out-String
    $alive = $r -match '"Success":true'
    "{0,-22} agent={1}" -f $name, $alive
}
Send 'key CTRL+ESC; delay 800; key R; delay 1200; type command; key RETURN'
Mark '1-windowed'
Send 'type echo AAA; key RETURN'
Mark '2-echo-aaa'
Send 'key ALT+RETURN'
Mark '3-fullscreen'
Send 'type echo BBB; key CTRL+M'
Mark '4-echo-bbb'
Send 'key ALT+RETURN'
Mark '5-returned'
& $c -Port $Port screenshot -OutFile (Join-Path $Out '5-returned-gdi.png') -Json 2>&1 | Out-String | Out-Null
Send 'type echo CCC; key RETURN'
Mark '6-echo-ccc'
Send 'type exit; key RETURN'
Mark '7-exit'
& $c -Port $Port screenshot -OutFile (Join-Path $Out '7-exit-gdi.png') -Json 2>&1 | Out-String | Out-Null

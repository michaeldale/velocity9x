param(
    [Parameter(Mandatory)][string]$Address,
    [Parameter(Mandatory)][string]$Out
)
# B5 on a physical machine: the 86Box dos-roundtrip.ps1 sequence, with the
# agent's ping after every step and its GDI screenshot where GDI owns the
# screen (before, windowed, after the return, after exit). No host capture.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
New-Item -ItemType Directory -Force $Out | Out-Null
function Send([string]$sequence) {
    & $c -Host $Address input -Sequence $sequence -Json 2>&1 | Out-String | Out-Null
}
function Mark([string]$name, [switch]$Shot) {
    Start-Sleep -Seconds 4
    $r = & $c -Host $Address info -ConnectTimeoutSeconds 15 -Json 2>&1 | Out-String
    $alive = $r -match '"Success":true'
    $mode = ''
    if ($r -match '"ScreenWidth":(\d+),"ScreenHeight":(\d+)') { $mode = "$($Matches[1])x$($Matches[2])" }
    if ($Shot -and $alive) {
        & $c -Host $Address screenshot -OutFile (Join-Path $Out "$name.png") -Json 2>&1 | Out-String | Out-Null
    }
    "{0,-14} agent={1} screen={2}" -f $name, $alive, $mode
}
Mark '0-before' -Shot
Send 'key CTRL+ESC; delay 800; key R; delay 1500; type command; key RETURN'
Mark '1-windowed' -Shot
Send 'type echo AAA; key RETURN'
Mark '2-echo-aaa' -Shot
Send 'key ALT+RETURN'
Mark '3-fullscreen'
Send 'type echo BBB; key CTRL+M'
Mark '4-echo-bbb'
Send 'key ALT+RETURN'
Mark '5-returned' -Shot
Send 'type echo CCC; key RETURN'
Mark '6-echo-ccc' -Shot
Send 'type exit; key RETURN'
Mark '7-exit' -Shot

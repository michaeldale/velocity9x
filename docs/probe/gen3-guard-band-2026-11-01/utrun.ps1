param([Parameter(Mandatory = $true)][string]$Tag,
      [switch]$Guard,
      [string]$Root = 'C:\everything\velocity9x\docs\probe\gen3-guard-band-2026-11-01\step2-ut99')
# UT99's CityIntro flyby on the netbook (Direct3D, 640x480x16 full screen),
# with the mask zero or the 1,024-pixel band, V9XTRACE snapshots either
# side, two screenshots, and the visible windows after.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
function Snap([string]$name) {
    $null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
    & $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $out "$name-V9XSNA7.INI") | Out-Null
}
$out = Join-Path $Root $Tag
New-Item -ItemType Directory -Force -Path $out | Out-Null
$mask = if ($Guard) { 0x100 -bor (1024 -shl 16) } else { 0 }
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -Arguments "-omit:$mask" -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
Snap 'pre'
(Select-String -LiteralPath (Join-Path $out 'pre-V9XSNA7.INI') -Pattern '^(Omit(Requested|Accepted)|BootId)=' | ForEach-Object Line) -join ' '
$null = J exec -Application 'C:\UnrealTournament\System\UnrealTournament.exe' -WorkingDirectory 'C:\UnrealTournament\System' -Detach
$null = J wait-window -Title 'Unreal Tournament' -WaitSeconds 240
"window exit=$LASTEXITCODE"
# A run that was killed leaves UT99 to start in its Recovery Mode dialog;
# its first button runs the game as normal.
$w = J windows
if (@($w.Windows | Where-Object { $_.Title -like '*Recovery Mode*' }).Count -gt 0) {
    $null = J input -Sequence 'move 512,325; click left'
    'recovery dialog: Run Unreal Tournament'
    Start-Sleep -Seconds 10
}
# The sample is a fixed length of flyby, not a wait for anything; a
# guest-side 30 s input delay outlived the agent's connection.
Start-Sleep -Seconds 30
& $c screenshot @conn -OutFile (Join-Path $out 'flyby-30s.png') | Out-Null
Start-Sleep -Seconds 30
& $c screenshot @conn -OutFile (Join-Path $out 'flyby-60s.png') | Out-Null
Snap 'post'
# Quit through the console, so the next start is not a recovery start;
# killed only if that does not end it.
$null = J input -Sequence 'type `; delay 1000; type quit; key ENTER'
$null = J wait-process -Name 'UNREALTOURNAMENT.EXE' -WaitSeconds 45
if ($LASTEXITCODE -ne 0) {
    $null = J kill -Name 'UNREALTOURNAMENT.EXE'
    $null = J wait-process -Name 'UNREALTOURNAMENT.EXE' -WaitSeconds 60
    'UT99 killed'
}
& $c get @conn -Source 'C:\UnrealTournament\System\UnrealTournament.log' -Destination (Join-Path $out 'UnrealTournament.log') | Out-Null
$w = J windows
"windows: " + (@($w.Windows | ForEach-Object { "$($_.Title) [$($_.Class)]" }) -join ' | ')
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -Arguments '-omit:0' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
"$Tag done; mask 0"

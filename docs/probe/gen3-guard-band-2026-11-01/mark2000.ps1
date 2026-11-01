param([Parameter(Mandatory = $true)][string]$OutDir)
# 3DMark 2000's default benchmark on the netbook (1024x576x16, its own
# choice), with V9XTRACE snapshots either side, as the 0.34.0 release check
# drove it (gamesnet.ps1).
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
function V { & $c @args @conn }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
V get -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $OutDir '3dmark-pre-V9XSNA7.INI') | Out-Null
$null = J exec -Application 'C:\Program Files\MadOnion.com\3DMark2000\3DMark2000.exe' -WorkingDirectory 'C:\Program Files\MadOnion.com\3DMark2000' -Detach
$null = J wait-window -Title '3DMark2000' -WaitSeconds 120
$null = J input -Sequence 'move 481,262; click left; delay 3000; move 510,343; click left; delay 3000; move 448,408; click left'
$titles = ''
for ($i = 0; $i -lt 8; ++$i) {
    $w = J windows
    $titles = @($w.Windows | ForEach-Object { $_.Title }) -join ' | '
    if ($titles -match 'Overall Score') { break }
    $null = J wait-window -Title 'Overall Score' -WaitSeconds 120
}
"3DMark windows: $titles"
V screenshot -OutFile (Join-Path $OutDir '3dmark-score.png') | Out-Null
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
V get -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $OutDir '3dmark-post-V9XSNA7.INI') | Out-Null
$null = J input -Sequence 'key enter'
$null = J kill -Name '3DMARK2000.EXE'
$null = J wait-process -Name '3DMARK2000.EXE' -WaitSeconds 60

param([Parameter(Mandatory = $true)][string]$OutDir, [string]$Grab = '-grab:300x10')
# Frame grabs across 3DMark 2001 SE's benchmark on the netbook. The grab is
# armed before the benchmark starts (a V9XTRACE run during a 3DMark test has
# killed the test before); 3DMark is at its main or score window.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
$s = 'C:\Users\michael\AppData\Local\Temp\claude\C--everything-velocity9x\4be3f200-39f9-44aa-8aa4-c90363b7ee94\scratchpad'
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$w = J windows
if (@($w.Windows | Where-Object { $_.Title -like '*Overall Score*' }).Count -gt 0) {
    $null = J input -Sequence 'move 612,400; click left; delay 2000'
}
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -Arguments $Grab -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
& $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $OutDir 'armed-V9XSNA7.INI') | Out-Null
(Select-String -LiteralPath (Join-Path $OutDir 'armed-V9XSNA7.INI') -Pattern '^(FrameGrab\w+|BootId)=' | ForEach-Object Line) -join ' '
& (Join-Path $s 'mark2001.ps1') -OutDir $OutDir
(Select-String -LiteralPath (Join-Path $OutDir 'post-V9XSNA7.INI') -Pattern '^FrameGrab\w+=' | ForEach-Object Line) -join ' '
for ($i = 0; $i -lt 10; ++$i) {
    & $c get @conn -Source "C:\V9XDIAG\V9XGRAB$i.BMP" -Destination (Join-Path $OutDir "V9XGRAB$i.BMP") | Out-Null
}
Get-ChildItem -LiteralPath $OutDir -Filter 'V9XGRAB*.BMP' | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize | Out-String

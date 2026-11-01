param([Parameter(Mandatory = $true)][string]$OutDir)
# 3DMark 2001 SE's default benchmark on the netbook, already started at its
# main window: click Benchmark, wait for a window other than the main one
# and the desktop's, then screenshot, snapshot and list the windows.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
$null = J input -Sequence 'move 575,412; click left'
"$(Get-Date -Format HH:mm:ss) Benchmark clicked"
$titles = ''
for ($i = 0; $i -lt 30; ++$i) {
    $null = J wait-window -Title 'Score' -WaitSeconds 120
    $w = J windows
    $titles = @($w.Windows | ForEach-Object { "$($_.Title) [$($_.Class)]" }) -join ' | '
    $other = @($w.Windows | Where-Object {
        $_.Title -and $_.Title -ne 'Untitled - 3DMark2001 SE' -and
        $_.Title -ne 'Program Manager' -and $_.Class -ne 'Shell_TrayWnd' })
    if ($other.Count -gt 0 -and -not ($titles -match '3DMark2001 SE:400000' -and $other.Count -eq 0)) {
        # A full-screen test window has an empty or class-only title; stop
        # on anything that looks like a dialog.
        if (@($other | Where-Object { $_.Class -eq '#32770' -or $_.Title -match 'Result|Score|Error|illegal' }).Count -gt 0) { break }
    }
}
"$(Get-Date -Format HH:mm:ss) windows: $titles"
& $c screenshot @conn -OutFile (Join-Path $OutDir 'end.png') | Out-Null
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
& $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $OutDir 'post-V9XSNA7.INI') | Out-Null

param([string]$TargetHost, [string]$Relay, [string]$OutFile)
# Run V9XRTLK.EXE on one machine and save its report.
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$c = if ($Relay) { @{ Relay = $Relay } } else { @{ Host = $TargetHost } }
& $ctl put @c -Source 'C:\everything\velocity9x\build\rtleak-probe\V9XRTLK.EXE' -Destination 'C:\V9XDIAG\V9XRTLK.EXE' -ConnectTimeoutSeconds 120 -Json | Out-Null
$r = & $ctl exec @c -Application 'C:\V9XDIAG\V9XRTLK.EXE' -WaitForExit -WaitSeconds 120 -ConnectTimeoutSeconds 120 -Json | ConvertFrom-Json
"exit $($r.ExitCode) boot $($r.BootCounter)"
$text = (& $ctl cat @c -Path 'C:\V9XDIAG\V9XRTLK.INI' -ConnectTimeoutSeconds 120 -Json | ConvertFrom-Json).Text
Set-Content -LiteralPath $OutFile -Value $text -Encoding Ascii
$text

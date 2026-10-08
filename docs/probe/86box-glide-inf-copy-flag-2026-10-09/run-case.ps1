param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Before,          # file put in SYSTEM first; empty = leave as is
    [string]$Port = '9878'
)
# One SetupX copy-flag case: optionally seed SYSTEM\GLIDE2X.DLL, install
# GTEST.INF's DefaultInstall with the 0.14.0 DLL as source, and fetch what
# SYSTEM holds afterwards.
$ErrorActionPreference = 'Stop'
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$h = '127.0.0.1'
$here = $PSScriptRoot
$o = Join-Path $here $Name
New-Item -ItemType Directory -Force $o | Out-Null
$new = 'C:\everything\velocity9x\build\glide\glide2x.dll'
if ($Before) {
    & $c put -EndpointHost $h -Port $Port -Source $Before -Destination 'C:\WINDOWS\SYSTEM\GLIDE2X.DLL' -Json | Out-Null
}
& $c put -EndpointHost $h -Port $Port -Source (Join-Path $here 'GTEST.INF') -Destination 'C:\V9XDIAG\GTEST.INF' -Json | Out-Null
& $c put -EndpointHost $h -Port $Port -Source $new -Destination 'C:\V9XDIAG\GLIDE2X.DLL' -Json | Out-Null
$r = & $c exec -EndpointHost $h -Port $Port -Application 'C:\WINDOWS\RUNDLL.EXE' -Arguments 'setupx.dll,InstallHinfSection DefaultInstall 132 C:\V9XDIAG\GTEST.INF' -WorkingDirectory 'C:\V9XDIAG' -TimeoutSeconds 60 -Json
"exec: $r"
& $c get -EndpointHost $h -Port $Port -Source 'C:\WINDOWS\SYSTEM\GLIDE2X.DLL' -Destination (Join-Path $o 'AFTER.DLL') -Json | Out-Null
$after = Get-Item (Join-Path $o 'AFTER.DLL')
$hash = (Get-FileHash $after.FullName).Hash
$names = @{}
$names[(Get-FileHash $new).Hash] = 'velocity9x 0.14.0 (source)'
if ($Before) { $names[(Get-FileHash $Before).Hash] = "before: $Before" }
"after: $($after.Length) bytes, version $($after.VersionInfo.FileVersion), = $($names[$hash])"

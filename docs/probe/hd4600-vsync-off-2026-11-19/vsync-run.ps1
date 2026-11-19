param(
    [Parameter(Mandatory = $true)][string]$OutDir,
    [Parameter(Mandatory = $true)][ValidateSet('0', '1', '2')][string]$VSync,
    [int]$Minutes = 3,
    [string]$GrabArg = "-grab:40x10"
)
# Set [Velocity9x] VSync= in the HD 4600's SYSTEM.INI (read by DirectDraw at
# driver-object creation, no restart), then run 3DMark 2001 SE with grabs and
# V9XTRACE snapshots before and after (hsw-3dm2001.ps1).
$ErrorActionPreference = 'Stop'
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$h = '10.0.1.254'
$runner = 'C:\Users\michael\AppData\Local\Temp\claude\d0f9ac92ab0a27f1\ab3e0752-8ba1-4d96-8051-f55daba129dd\scratchpad\hsw-3dm2001.ps1'
New-Item -ItemType Directory -Force $OutDir | Out-Null

$before = Join-Path $OutDir 'SYSTEM-before.INI'
& $c get -Host $h -Source 'C:\WINDOWS\SYSTEM.INI' -Destination $before | Out-Null
$lines = [System.Collections.Generic.List[string]]::new()
$section = ''
$found = $false
foreach ($line in (Get-Content -LiteralPath $before)) {
    if ($line -match '^\s*\[(.+)\]\s*$') { $section = $Matches[1] }
    if ($section -eq 'Velocity9x' -and $line -match '^\s*VSync\s*=') { continue }
    $lines.Add($line)
    if ($line -match '^\s*\[Velocity9x\]\s*$') {
        $lines.Add("VSync=$VSync")
        $found = $true
    }
}
if (-not $found) {
    $lines.Add('[Velocity9x]')
    $lines.Add("VSync=$VSync")
}
$after = Join-Path $OutDir 'SYSTEM-after.INI'
[System.IO.File]::WriteAllLines($after, $lines, [System.Text.Encoding]::GetEncoding(1252))
& $c put -Host $h -Source $after -Destination 'C:\WINDOWS\SYSTEM.INI' | Out-Null
$check = (& $c cat -Host $h -Path 'C:\WINDOWS\SYSTEM.INI' -Json | ConvertFrom-Json).Text
if ($check -notmatch "(?m)^VSync=$VSync\s*$") { throw 'VSync= did not reach SYSTEM.INI' }
"VSync=$VSync set"

& $runner -OutDir $OutDir -Minutes $Minutes -Grab -GrabArg $GrabArg

param(
    [Parameter(Mandatory)] [string]$Target,      # M-XXXXXX or host:port
    [Parameter(Mandatory)] [string]$Family,      # ati, intel-gma
    [Parameter(Mandatory)] [string]$OutDir,
    [string[]]$D32Runs = @(''),                  # '' | '/ddi7' | 'bpp16' | 'bpp32'
    [int]$ResetWait = 600,
    [switch]$SkipDeploy,
    [switch]$SkipProbes,
    [string]$PackageDir
)
$ErrorActionPreference = 'Stop'
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$repo = 'C:\everything\velocity9x'
$pkg = if ($PackageDir) { $PackageDir } else { Join-Path $repo "build\win98se-$Family" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
if ($Target -like 'M-*') { $conn = @{ Relay = $Target } } else { $parts = $Target.Split(':'); $conn = @{ EndpointHost = $parts[0]; Port = [int]$parts[1] } }
function V { & $c @args @conn -ConnectTimeoutSeconds 30 }
function J { (& $c @args @conn -ConnectTimeoutSeconds 30 -Json) | ConvertFrom-Json }
# Uploads retry: a relay put of the 800 KB HAL timed out once.
function Put {
    param([string]$Source, [string]$Destination)
    for ($try = 1; $try -le 4; ++$try) {
        $r = (& $c put -Source $Source -Destination $Destination @conn -ConnectTimeoutSeconds 120 -Json) | ConvertFrom-Json
        if ($r -and $r.Success) { return $r }
        Write-Host "put $Destination attempt $try failed; retrying"
    }
    throw "put $Destination failed"
}

$files = [ordered]@{
    'V9XDISP.DRV' = 'C:\V9XNDRV.BIN'; 'V9XMINI.VXD' = 'C:\V9XNVXD.BIN'
    'V9XHAL.DLL' = 'C:\V9XNHAL.BIN'; 'V9XSETP.DLL' = 'C:\V9XNSTP.BIN'
    'V9XGL.DLL' = 'C:\V9XNGL.BIN'; 'GLIDE2X.DLL' = 'C:\V9XNG2.BIN'
    'GLIDE3X.DLL' = 'C:\V9XNG3.BIN'; 'ATI3DCIF.DLL' = 'C:\V9XNCIF.BIN'
}

if (-not $SkipDeploy) {
    $st = J stat -RemotePath 'C:\WINDOWS\WININIT.INI'
    if ($st.Exists) { throw "$Target has a pending WININIT.INI; stopping." }
    $lines = @('[Rename]')
    foreach ($k in $files.Keys) {
        $null = Put -Source (Join-Path $pkg $k) -Destination $files[$k]
        $lines += "C:\WINDOWS\SYSTEM\$k=$($files[$k])"
    }
    $w = Join-Path $OutDir 'WININIT.INI'
    [IO.File]::WriteAllText($w, (($lines -join "`r`n") + "`r`n"), [Text.Encoding]::ASCII)
    $r = J put -Source $w -Destination 'C:\WINDOWS\WININIT.INI'
    if (-not $r.Success) { throw 'put WININIT failed' }
    $rs = J reset -JobId "g3omit-$($Target -replace '[^A-Za-z0-9]','')" -WaitSeconds $ResetWait
    "reset: boot $($rs.PreviousBootCounter) -> $($rs.BootCounter) desktop=$($rs.DesktopReady)"
}

$bad = 0
foreach ($k in $files.Keys) {
    $s = J stat -RemotePath $files[$k]
    if ($s.Exists) { "STAGED STILL PRESENT: $($files[$k])"; $bad++ }
    $h = J hash -RemotePath "C:\WINDOWS\SYSTEM\$k"
    $local = (Get-FileHash -LiteralPath (Join-Path $pkg $k) -Algorithm SHA256).Hash
    if ($h.Sha256 -ne $local) { "HASH MISMATCH: $k"; $bad++ }
}
"install check: $(8 - $bad) of 8 ok (mismatches $bad)"
if ($bad -ne 0) { throw 'install not as built' }
if ($SkipProbes) { return }

V mkdir -RemotePath 'C:\V9XREL' | Out-Null
$probes = @{
    'C:\V9XREL\V9XD32.EXE' = "$repo\build\d3d32-probe\V9XD32.EXE"
    'C:\V9XREL\V9XGLP.EXE' = "$repo\build\gl-probe\V9XGLP.EXE"
    'C:\V9XREL\V9XGLIDP.EXE' = "$repo\build\glide-probe\V9XGLIDP.EXE"
    'C:\V9XREL\V9XR3DP.EXE' = "$repo\build\r3d-probe\V9XR3DP.EXE"
    'C:\V9XREL\V9XTRACE.EXE' = "$pkg\V9XTRACE.EXE"
    'C:\V9XREL\V9XMSW.EXE' = "$pkg\V9XMSW.EXE"
}
foreach ($k in $probes.Keys) { $r = Put -Source $probes[$k] -Destination $k }

foreach ($run in $D32Runs) {
    $args2 = ''
    $tag = 'default'
    if ($run -like 'bpp*') {
        $bpp = $run.Substring(3)
        $m = J exec -Application 'C:\V9XREL\V9XMSW.EXE' -Arguments "/set:1024x768x$bpp" -WorkingDirectory 'C:\V9XREL' -TimeoutSeconds 120
        $tag = $run
    } elseif ($run) {
        $args2 = $run; $tag = $run.TrimStart('/')
    }
    if ($args2) {
        $r = J exec -Application 'C:\V9XREL\V9XD32.EXE' -Arguments $args2 -WorkingDirectory 'C:\V9XREL' -TimeoutSeconds 300
    } else {
        $r = J exec -Application 'C:\V9XREL\V9XD32.EXE' -WorkingDirectory 'C:\V9XREL' -TimeoutSeconds 300
    }
    V get -Source 'C:\V9XDIAG\V9XD32.TXT' -Destination (Join-Path $OutDir "v9xd32-$tag-V9XD32.TXT") | Out-Null
    $res = (Select-String -LiteralPath (Join-Path $OutDir "v9xd32-$tag-V9XD32.TXT") -Pattern '^(Result|Failures)=' | ForEach-Object { $_.Line }) -join ' '
    "V9XD32 $tag exit=$($r.ExitCode): $res"
}
foreach ($pr in @(@('V9XGLP', 'V9XGLP.INI', 'glp'), @('V9XGLIDP', 'V9XGLIDP.INI', 'glidp'), @('V9XR3DP', 'V9XR3DP.INI', 'r3dp'))) {
    $r = J exec -Application "C:\V9XREL\$($pr[0]).EXE" -WorkingDirectory 'C:\V9XREL' -TimeoutSeconds 300
    $dest = Join-Path $OutDir "$($pr[2])-$($pr[1])"
    V get -Source "C:\V9XDIAG\$($pr[1])" -Destination $dest | Out-Null
    $res = if (Test-Path -LiteralPath $dest) { (Select-String -LiteralPath $dest -Pattern '^(Result|Failures|Build)=' | ForEach-Object { $_.Line }) -join ' ' } else { 'no output' }
    "$($pr[0]) exit=$($r.ExitCode): $res"
}

param([string[]]$Only)
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$ep = @{ EndpointHost = '10.0.1.254'; Port = 9869 }
$d = 'C:\Users\michael\AppData\Local\Temp\claude\C--everything-velocity9x\ec34a013-22be-46ea-b8cf-e299eaf4c783\scratchpad\hsw\cif'
$shot = 'C:\Users\michael\AppData\Local\Temp\claude\C--everything-velocity9x\ec34a013-22be-46ea-b8cf-e299eaf4c783\scratchpad\hsw\shot.ps1'
$demos = @(
    @('cubes',  'C:\3DCIF\CUBES',  'ATICUBES.EXE'),
    @('ati3d',  'C:\3DCIF\ATi-3D', 'ATi-3D.exe'),
    @('pixie',  'C:\3DCIF\PIXIE',  'PIXIE.EXE'),
    @('fairy',  'C:\3DCIF\FAIRY',  'FAIRY.EXE'),
    @('laputa', 'C:\3DCIF\LAPUTA', 'LAPUTA.EXE'),
    @('kdemo',  'C:\3DCIF\Knight', 'DEMO.EXE'),
    @('kmorph', 'C:\3DCIF\Knight', 'MORPH.EXE'),
    @('kvq',    'C:\3DCIF\Knight', 'VQ.EXE')
)
foreach ($demo in $demos) {
    if ($Only -and ($Only -notcontains $demo[0])) { continue }
    $null = & $ctl exec @ep -Application "$($demo[1])\$($demo[2])" -WorkingDirectory $demo[1] -Detach -Json
    Start-Sleep -Seconds 20
    & $shot -Name "cif-$($demo[0])" -Sequence 'move 320,240' -WaitMs 500 | Out-Null
    Start-Sleep -Seconds 20
    $null = & $ctl input @ep -Sequence 'key ESCAPE' -Json
    Start-Sleep -Seconds 8
    $null = & $ctl get @ep -Source 'C:\V9XDIAG\V9XCIF.LOG' -Destination "$d\V9XCIF-$($demo[0]).LOG" -Json
    $last = Get-Content "$d\V9XCIF-$($demo[0]).LOG" | Select-String -Pattern 'summary (term|detach)' | Select-Object -Last 1
    "$($demo[0]): $($last.Line)"
}

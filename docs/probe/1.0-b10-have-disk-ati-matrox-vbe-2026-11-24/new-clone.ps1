param(
    [Parameter(Mandatory)] [string]$Name,       # e.g. Win98SE-B10-Mach64
    [Parameter(Mandatory)] [string]$Pipe,       # e.g. b10-mach64-com1
    [Parameter(Mandatory)] [string]$Card,       # 86Box gfxcard internal name
    [string]$CardSection = '',                  # extra cfg text replacing the ViRGE section
    [Parameter(Mandatory)] [string]$Mac         # e.g. 3a:10:01
)
# A fresh clone of the S3 reference VM (never had Velocity9x) with another
# emulated card, host port 9879 / VNC 5979, its own uuid, MAC and COM pipe.
$ErrorActionPreference = 'Stop'
$root = 'C:\Users\michael\86Box VMs'
$src = Join-Path $root 'Win98SE-Native-S3'
$dst = Join-Path $root $Name
if (Test-Path -LiteralPath $dst) { throw "$dst exists" }
New-Item -ItemType Directory -Path $dst | Out-Null
Copy-Item -LiteralPath (Join-Path $src 'Win98HDD.vhd') -Destination $dst
Copy-Item -LiteralPath (Join-Path $src 'nvr') -Destination $dst -Recurse
$cfg = [IO.File]::ReadAllText((Join-Path $src '86box.cfg'))
$cfg = $cfg -replace 'uuid = [0-9a-f-]+', ('uuid = ' + [guid]::NewGuid().ToString())
$cfg = $cfg -replace 'mac = 5f:89:32', ('mac = ' + $Mac)
$cfg = $cfg -replace 'native-s3-com1', $Pipe
$cfg = $cfg -replace '0_external = 9870', '0_external = 9879'
$cfg = $cfg -replace '1_external = 5970', '1_external = 5979'
$cfg = $cfg -replace 'gfxcard = virge_dx_pci', ('gfxcard = ' + $Card)
$cfg = $cfg -replace '\[S3 ViRGE/DX PCI\]\r?\nbios = virge375_pci\r?\n', $CardSection
[IO.File]::WriteAllText((Join-Path $dst '86box.cfg'), $cfg)
$h1 = (Get-FileHash -LiteralPath (Join-Path $src 'Win98HDD.vhd')).Hash
$h2 = (Get-FileHash -LiteralPath (Join-Path $dst 'Win98HDD.vhd')).Hash
"VHD reference $h1"
"VHD clone     $h2"
Select-String -LiteralPath (Join-Path $dst '86box.cfg') -Pattern 'gfxcard|uuid|mac =|external|com1|\[Matrox' |
    ForEach-Object { $_.Line }

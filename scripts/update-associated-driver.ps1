[CmdletBinding()]
param(
    [string]$PackagePath,
    # Path to the remote-agent controller (v9xctl.ps1). Set V9X_AGENT_CTL to
    # avoid passing it on every call; the agent lives outside this repository.
    [string]$ControllerPath = $env:V9X_AGENT_CTL,
    # Remote-agent host. Defaults to the loopback the 86Box guests are reached
    # on; pass the address of a physical target such as BARRY.
    [string]$GuestHost = '127.0.0.1',
    # Remote-agent host port. The project runs more than one guest, so the
    # controller default is not always the intended target.
    [ValidateRange(1, 65535)]
    [int]$Port = 9869,
    [string]$JobId = ("update-{0}" -f (Get-Date -Format "yyyyMMdd-HHmmss")),
    [string]$ResultsDirectory,
    [ValidateRange(30, 600)]
    [int]$BootTimeoutSeconds = 180,
    [switch]$NoReboot,
    [switch]$Json
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "lib\agent.ps1")
if (-not $PackagePath) {
    $PackagePath = Join-Path $repoRoot "build\vm-probe\ACTIVE"
}
if (-not $ResultsDirectory) {
    $ResultsDirectory = Join-Path $repoRoot "build\driver-results\$JobId"
}
if ($JobId -notmatch '^[A-Za-z0-9._-]+$') {
    throw "JobId contains unsupported characters."
}
$package = [IO.Path]::GetFullPath($PackagePath)
$results = [IO.Path]::GetFullPath($ResultsDirectory)
if (-not $ControllerPath) {
    throw "Specify -ControllerPath (or set V9X_AGENT_CTL) to the remote agent's v9xctl.ps1."
}
foreach ($path in @($ControllerPath, $package)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required path does not exist: $path"
    }
}
foreach ($file in @("V9XDISP.DRV", "V9XMINI.VXD", "V9X16LD.EXE",
                     "V9XHAL.DLL", "V9XSETP.DLL")) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $file))) {
        throw "Driver package is missing $file."
    }
}
New-Item -ItemType Directory -Force -Path $results | Out-Null
$guestJob = "C:\V9XREMOTE\JOBS\$JobId"

# One v9xctl call, in this process (a powershell.exe per call cost a process
# start each). Transport failures are retried by v9xctl itself for up to
# 15 s (-WaitForAgent); any other failure throws with v9xctl's own message.
# A hashtable splat: an array splat binds positionally in a script call.
function Invoke-V9xCtlJson {
    param([string]$Operation, [hashtable]$Arguments = @{}, [switch]$AllowFailure)
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = @(& $ControllerPath $Operation @Arguments -EndpointHost $GuestHost `
            -Port $Port -WaitForAgent 15 -Json 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    $jsonLine = $lines | Where-Object {
        $_ -is [string] -and $_.TrimStart().StartsWith("{")
    } | Select-Object -Last 1
    if ($exitCode -eq 0 -and $jsonLine) {
        return $jsonLine | ConvertFrom-Json
    }
    if ($AllowFailure) {
        return $null
    }
    throw "v9xctl $Operation failed (exit $exitCode): $($lines -join [Environment]::NewLine)"
}

# The agent first (lib\agent.ps1): the steps below use reg, hash, stat and
# reboot -DismissLogon.
$initialInfo = Update-V9xAgent -Package (Get-V9xAgentPackage -ControllerPath $ControllerPath) `
    -Call { param($Verb, $Arguments) Invoke-V9xCtlJson $Verb $Arguments -AllowFailure } `
    -Log { param($Text) Write-Verbose $Text -Verbose }

$upload = Invoke-V9xCtlJson push-tree @{ Source = $package; Destination = $guestJob }
$preflight = Invoke-V9xCtlJson exec @{
    Application = "$guestJob\V9X16LD.EXE"; Arguments = "/quiet"
    WorkingDirectory = $guestJob; TimeoutSeconds = 120
}

# The display class must already name this driver. Read with the agent's
# registry query, one key per call (was REGEDIT /E through COMMAND.COM and a
# download of the export).
$classKey = 'HKLM\System\CurrentControlSet\Services\Class\Display'
$class = Invoke-V9xCtlJson reg @{ Key = $classKey }
$binding = @()
$associated = $false
foreach ($index in @($class.Subkeys | Where-Object { $_ -match '^\d{4}$' } | Sort-Object)) {
    $driverKey = Invoke-V9xCtlJson reg @{ Key = "$classKey\$index" } -AllowFailure
    $defaults = Invoke-V9xCtlJson reg @{ Key = "$classKey\$index\DEFAULT" } -AllowFailure
    $drv = @($defaults.Values | Where-Object { $_.Name -eq 'drv' }) | Select-Object -First 1
    $description = @($driverKey.Values | Where-Object { $_.Name -eq 'DriverDesc' }) | Select-Object -First 1
    $binding += [pscustomobject]@{ Index = $index; DriverDesc = $description.Data; Drv = $drv.Data }
    $named = @($driverKey.Values | Where-Object { "$($_.Data)" -match 'Velocity9x' }).Count -gt 0
    if ($drv -and [string]$drv.Data -ieq 'v9xdisp.drv' -and $named) {
        $associated = $true
    }
}
$binding | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $results "DISPLAY-BEFORE.json") -Encoding UTF8
if (-not $associated) {
    throw "The active display class is not already associated with Velocity9x."
}

# Windows 98 validates Controls Folder property-sheet handlers with a
# machine-specific DWORD named Tag.  Recover the machine seed from any
# existing, validated Display handler and use it for the Velocity9x CLSID.
$handlersKey = 'HKLM\Software\Microsoft\Windows\CurrentVersion\Controls Folder\Display\shellex\PropertySheetHandlers'
$handlers = Invoke-V9xCtlJson reg @{ Key = $handlersKey }
$taggedClsid = $null
$existingTag = $null
$handlerRecord = @()
foreach ($handler in @($handlers.Subkeys)) {
    $values = Invoke-V9xCtlJson reg @{ Key = "$handlersKey\$handler" } -AllowFailure
    $clsid = @($values.Values | Where-Object { $_.Name -eq '(Default)' }) | Select-Object -First 1
    $tag = @($values.Values | Where-Object { $_.Name -eq 'Tag' }) | Select-Object -First 1
    $handlerRecord += [pscustomobject]@{ Handler = $handler; Clsid = $clsid.Data; Tag = $tag.Data; TagType = $tag.Type }
    if ($taggedClsid -or -not $clsid -or [string]$clsid.Data -notmatch '^\{[0-9A-Fa-f-]{36}\}$' -or -not $tag) {
        continue
    }
    if ($tag.Type -eq 'REG_DWORD') {
        $existingTag = [uint32]$tag.Data
    } elseif ($tag.Type -eq 'REG_BINARY' -and [string]$tag.Data -match '^([0-9A-F]{2}) ([0-9A-F]{2}) ([0-9A-F]{2}) ([0-9A-F]{2})$') {
        # REG_BINARY comes back as hex bytes, least significant first.
        $existingTag = [Convert]::ToUInt32($Matches[4] + $Matches[3] + $Matches[2] + $Matches[1], 16)
    } else {
        continue
    }
    $taggedClsid = [string]$clsid.Data
}
$handlerRecord | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $results "DISPLAY-HANDLERS-BEFORE.json") -Encoding UTF8
if (-not $taggedClsid) {
    throw "No validated Windows 98 Display handler tag was available."
}
function Get-V9xTagWords([string]$Clsid) {
    $bytes = [Text.Encoding]::ASCII.GetBytes($Clsid.Substring(0, 8))
    @([BitConverter]::ToUInt32($bytes, 0),
      [BitConverter]::ToUInt32($bytes, 4))
}
$existingWords = Get-V9xTagWords $taggedClsid
$modulus = [uint64]0x100000000
$tagSeed = [uint32]((([uint64]$existingTag + (2 * $modulus) -
    [uint64]$existingWords[0] - [uint64]$existingWords[1]) % $modulus))
$settingsClsid = "{91925DA2-2EF0-4E20-B4E9-A53ED37E14B1}"
$settingsWords = Get-V9xTagWords $settingsClsid
$settingsTag = [uint32]((([uint64]$tagSeed +
    [uint64]$settingsWords[0] + [uint64]$settingsWords[1]) % $modulus))
$pending = Invoke-V9xCtlJson stat @{ RemotePath = 'C:\WINDOWS\WININIT.INI' }
if ($pending.Exists) {
    throw "C:\WINDOWS\WININIT.INI already exists; complete or inspect that pending update first."
}

# Staged under 8.3 names in an 8.3 folder, in one verified push (was COPY
# and FC /B through COMMAND.COM): real-mode WININIT cannot parse a longer
# source path.
$stageLocal = Join-Path $results 'stage'
New-Item -ItemType Directory -Force -Path $stageLocal | Out-Null
$staged = [ordered]@{
    'V9XDISP.DRV' = 'V9XNDRV.BIN'
    'V9XMINI.VXD' = 'V9XNVXD.BIN'
    'V9XHAL.DLL'  = 'V9XNHAL.BIN'
    'V9XSETP.DLL' = 'V9XNSET.BIN'
}
foreach ($name in $staged.Keys) {
    Copy-Item -LiteralPath (Join-Path $package $name) -Destination (Join-Path $stageLocal $staged[$name]) -Force
}
$null = Invoke-V9xCtlJson push-tree @{ Source = $stageLocal; Destination = 'C:\V9XSTAGE' }

# Plain dest=src renames, no NUL= lines: WININIT replaces an existing file
# on rename, and a NUL= deletion runs even when the rename then fails,
# which has left a guest with a 0-byte driver.
$wininitPath = Join-Path $results "WININIT.INI"
$wininit = @("[Rename]") + @($staged.Keys | ForEach-Object {
    "C:\WINDOWS\SYSTEM\$_=C:\V9XSTAGE\$($staged[$_])"
})
[IO.File]::WriteAllLines($wininitPath, $wininit, [Text.Encoding]::ASCII)
$stage = Invoke-V9xCtlJson put @{ Source = $wininitPath; Destination = "C:\WINDOWS\WININIT.INI" }

$settingsRegPath = Join-Path $results "V9XSETP.REG"
$settingsReg = @(
    "REGEDIT4",
    "",
    "[HKEY_CLASSES_ROOT\CLSID\{91925DA2-2EF0-4E20-B4E9-A53ED37E14B1}]",
    '@="Velocity9x Settings Page"',
    "",
    "[HKEY_CLASSES_ROOT\CLSID\{91925DA2-2EF0-4E20-B4E9-A53ED37E14B1}\InProcServer32]",
    '@="v9xsetp.dll"',
    '"ThreadingModel"="Apartment"',
    "",
    "[HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Controls Folder\Display\shellex\PropertySheetHandlers\Velocity9x]",
    '@="{91925DA2-2EF0-4E20-B4E9-A53ED37E14B1}"',
    ('"Tag"=dword:{0:x8}' -f $settingsTag),
    "",
    "[HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved]",
    '"{91925DA2-2EF0-4E20-B4E9-A53ED37E14B1}"="Velocity9x Settings Page"')
[IO.File]::WriteAllLines($settingsRegPath, $settingsReg, [Text.Encoding]::ASCII)
$null = Invoke-V9xCtlJson put @{ Source = $settingsRegPath; Destination = "C:\V9XREMOTE\TEMP\V9XSETP.REG" }
# REGEDIT is a Windows program: run directly, not through COMMAND.COM.
$settingsRegistration = Invoke-V9xCtlJson exec @{
    Application = 'C:\WINDOWS\REGEDIT.EXE'; Arguments = '/S C:\V9XREMOTE\TEMP\V9XSETP.REG'
    WorkingDirectory = 'C:\V9XREMOTE\TEMP'; TimeoutSeconds = 60
}

$reboot = $null
$screenshot = $null
if (-not $NoReboot) {
    # -DismissLogon: a startup logon dialog is cancelled and the reboot
    # waits for Explorer on the same connection.
    $reboot = Invoke-V9xCtlJson reboot @{
        JobId = $JobId; WaitSeconds = $BootTimeoutSeconds; DismissLogon = $true
    }
    foreach ($driverFile in $staged.Keys) {
        $compare = Invoke-V9xCtlJson hash @{
            RemotePath = "C:\WINDOWS\SYSTEM\$driverFile"; Source = (Join-Path $package $driverFile)
        } -AllowFailure
        if (-not $compare -or -not $compare.Matches) {
            throw "Installed $driverFile failed post-reboot verification."
        }
    }
    $screenshot = Invoke-V9xCtlJson screenshot @{ Destination = (Join-Path $results "DESKTOP.BMP") }
}

$summary = [pscustomobject]@{
    Success = $true
    Scope = "already-associated-driver-only"
    JobId = $JobId
    PackagePath = $package
    GuestJobPath = $guestJob
    InitialInfo = $initialInfo
    Upload = $upload
    Preflight = $preflight
    BootTimeStage = $stage
    SettingsRegistration = $settingsRegistration
    Reboot = $reboot
    Screenshot = $screenshot
}
$summary | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $results "update.json") -Encoding UTF8
if ($Json) { $summary | ConvertTo-Json -Depth 6 -Compress }
else { $summary | Format-List }

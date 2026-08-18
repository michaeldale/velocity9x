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
$powershell = Join-Path $PSHOME "powershell.exe"
function Invoke-V9xCtlJson {
    param([string]$Operation, [string[]]$OperationArguments = @())
    $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                   $ControllerPath, $Operation, "-Json",
                   "-EndpointHost", $GuestHost,
                   "-Port", [string]$Port) + $OperationArguments
    $lastFailure = ""
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        try {
            $lines = @(& $powershell @arguments 2>&1)
            $nativeExit = $LASTEXITCODE
        } catch {
            $lines = @($_.Exception.Message)
            $nativeExit = 1
        }
        $jsonLine = $lines | Where-Object {
            $_ -is [string] -and $_.TrimStart().StartsWith("{")
        } | Select-Object -Last 1
        if ($nativeExit -eq 0 -and $jsonLine) {
            return $jsonLine | ConvertFrom-Json
        }
        $lastFailure = $lines -join [Environment]::NewLine
        if ($attempt -lt 3) { Start-Sleep -Seconds 1 }
    }
    throw "v9xctl $Operation failed after 3 attempts: $lastFailure"
}

function Invoke-GuestShell {
    param([string]$Command)
    # Use the controller's canonical parameter name.
    # Backslash-escape embedded quotes so the nested Windows PowerShell
    # process preserves a quoted guest argument containing spaces.
    Invoke-V9xCtlJson shell @("-ShellCommand", $Command.Replace('"', '\"'))
}

$initialInfo = Invoke-V9xCtlJson info
$upload = Invoke-V9xCtlJson push-tree @(
    "-Source", $package, "-Destination", $guestJob)
$preflight = Invoke-V9xCtlJson exec @(
    "-Application", "$guestJob\V9X16LD.EXE", "-Arguments", "/quiet",
    "-WorkingDirectory", $guestJob, "-TimeoutSeconds", "120")

$null = Invoke-GuestShell (
    'REGEDIT /E C:\V9XREMOTE\V9XDISPLAY.REG ' +
    '"HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\Display"')
$bindingPath = Join-Path $results "DISPLAY-BEFORE.REG"
$null = Invoke-V9xCtlJson get @(
    "-Source", "C:\V9XREMOTE\V9XDISPLAY.REG", "-Destination", $bindingPath)
$binding = Get-Content -LiteralPath $bindingPath -Raw
if ($binding -notmatch '(?i)Velocity9x' -or
    $binding -notmatch '(?im)^"drv"="v9xdisp\.drv"\s*$') {
    throw "The active display class is not already associated with Velocity9x."
}

# Windows 98 validates Controls Folder property-sheet handlers with a
# machine-specific DWORD named Tag.  Recover the machine seed from any
# existing, validated Display handler and use it for the Velocity9x CLSID.
$null = Invoke-GuestShell (
    'REGEDIT /E C:\V9XREMOTE\DISPLAY-HANDLERS.REG ' +
    '"HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Controls Folder\Display\shellex\PropertySheetHandlers"')
$handlersPath = Join-Path $results "DISPLAY-HANDLERS-BEFORE.REG"
$null = Invoke-V9xCtlJson get @(
    "-Source", "C:\V9XREMOTE\DISPLAY-HANDLERS.REG",
    "-Destination", $handlersPath)
$handlers = Get-Content -LiteralPath $handlersPath -Raw
$taggedHandler = [regex]::Match(
    $handlers,
    '(?ims)^\[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Controls Folder\\Display\\shellex\\PropertySheetHandlers\\[^\]]+\]\s*\r?\n@="(\{[^"]+\})"\s*\r?\n"Tag"=dword:([0-9a-f]{8})')
if (-not $taggedHandler.Success) {
    throw "No validated Windows 98 Display handler tag was available."
}
function Get-V9xTagWords([string]$Clsid) {
    $bytes = [Text.Encoding]::ASCII.GetBytes($Clsid.Substring(0, 8))
    @([BitConverter]::ToUInt32($bytes, 0),
      [BitConverter]::ToUInt32($bytes, 4))
}
$existingWords = Get-V9xTagWords $taggedHandler.Groups[1].Value
$existingTag = [Convert]::ToUInt32($taggedHandler.Groups[2].Value, 16)
$modulus = [uint64]0x100000000
$tagSeed = [uint32]((([uint64]$existingTag + (2 * $modulus) -
    [uint64]$existingWords[0] - [uint64]$existingWords[1]) % $modulus))
$settingsClsid = "{91925DA2-2EF0-4E20-B4E9-A53ED37E14B1}"
$settingsWords = Get-V9xTagWords $settingsClsid
$settingsTag = [uint32]((([uint64]$tagSeed +
    [uint64]$settingsWords[0] + [uint64]$settingsWords[1]) % $modulus))
$pending = Invoke-GuestShell (
    "IF EXIST C:\WINDOWS\WININIT.INI ECHO EXISTS")
if ($pending.Stdout -match "EXISTS") {
    throw "C:\WINDOWS\WININIT.INI already exists; complete or inspect that pending update first."
}

$null = Invoke-GuestShell (
    "COPY /Y $guestJob\V9XDISP.DRV C:\V9XNDRV.BIN")
$null = Invoke-GuestShell (
    "COPY /Y $guestJob\V9XMINI.VXD C:\V9XNVXD.BIN")
$null = Invoke-GuestShell (
    "COPY /Y $guestJob\V9XHAL.DLL C:\V9XNHAL.BIN")
$null = Invoke-GuestShell (
    "COPY /Y $guestJob\V9XSETP.DLL C:\V9XNSET.BIN")
foreach ($pair in @(
    @("C:\V9XNDRV.BIN", "$guestJob\V9XDISP.DRV"),
    @("C:\V9XNVXD.BIN", "$guestJob\V9XMINI.VXD"),
    @("C:\V9XNHAL.BIN", "$guestJob\V9XHAL.DLL"),
    @("C:\V9XNSET.BIN", "$guestJob\V9XSETP.DLL"))) {
    $compare = Invoke-GuestShell "FC /B $($pair[0]) $($pair[1])"
    if ($compare.Stdout -notmatch "no differences encountered") {
        throw "Guest staging verification failed for $($pair[0])."
    }
}

$wininitPath = Join-Path $results "WININIT.INI"
$wininit = @(
    "[Rename]",
    "NUL=C:\WINDOWS\SYSTEM\V9XDISP.DRV",
    "C:\WINDOWS\SYSTEM\V9XDISP.DRV=C:\V9XNDRV.BIN",
    "NUL=C:\WINDOWS\SYSTEM\V9XMINI.VXD",
    "C:\WINDOWS\SYSTEM\V9XMINI.VXD=C:\V9XNVXD.BIN",
    "NUL=C:\WINDOWS\SYSTEM\V9XHAL.DLL",
    "C:\WINDOWS\SYSTEM\V9XHAL.DLL=C:\V9XNHAL.BIN",
    "NUL=C:\WINDOWS\SYSTEM\V9XSETP.DLL",
    "C:\WINDOWS\SYSTEM\V9XSETP.DLL=C:\V9XNSET.BIN")
[IO.File]::WriteAllLines($wininitPath, $wininit, [Text.Encoding]::ASCII)
$stage = Invoke-V9xCtlJson put @(
    "-Source", $wininitPath, "-Destination", "C:\WINDOWS\WININIT.INI")

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
$null = Invoke-V9xCtlJson put @(
    "-Source", $settingsRegPath,
    "-Destination", "C:\V9XREMOTE\TEMP\V9XSETP.REG")
$settingsRegistration = Invoke-GuestShell (
    "REGEDIT /S C:\V9XREMOTE\TEMP\V9XSETP.REG")

$reboot = $null
$desktop = $null
$screenshot = $null
if (-not $NoReboot) {
    $reboot = Invoke-V9xCtlJson reboot @(
        "-JobId", $JobId, "-WaitSeconds", [string]$BootTimeoutSeconds)
    $desktop = Invoke-V9xCtlJson wait-desktop @(
        "-WaitSeconds", [string]$BootTimeoutSeconds)
    foreach ($driverFile in @("V9XDISP.DRV", "V9XMINI.VXD", "V9XHAL.DLL",
                              "V9XSETP.DLL")) {
        $compare = Invoke-GuestShell (
            "FC /B C:\WINDOWS\SYSTEM\$driverFile $guestJob\$driverFile")
        if ($compare.Stdout -notmatch "no differences encountered") {
            throw "Installed $driverFile failed post-reboot verification."
        }
    }
    $screenshot = Invoke-V9xCtlJson screenshot @(
        "-Destination", (Join-Path $results "DESKTOP.BMP"))
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
    Desktop = $desktop
    Screenshot = $screenshot
}
$summary | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $results "update.json") -Encoding UTF8
if ($Json) { $summary | ConvertTo-Json -Depth 6 -Compress }
else { $summary | Format-List }

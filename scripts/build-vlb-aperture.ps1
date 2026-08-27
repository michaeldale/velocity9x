# Build the VLB linear-aperture probe.
#
# This is deliberately NOT build-vga-survey.ps1, and the difference is the
# point. That script exists to prove a tool is safe enough to hand to a
# stranger: it refuses to compile a mode set, a PCI write, or a port constant
# outside an audited list. This one compiles a tool that sets a video mode,
# writes the card's aperture registers, and enters protected mode - none of
# which could pass that gate, and none of which should.
#
# So the gate here asserts the opposite kind of thing. Not "this cannot touch
# the machine", which is false, but "this says so, refuses to run where it would
# be unsafe, and puts back what it changed".
[CmdletBinding()]
param([string]$BuildId)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\vlb-aperture"
$source = Join-Path $repoRoot "tools\diag\vlb_aperture_dos.c"
. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "vlb-aperture-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw "Invalid BuildId" }

$sourceText = Get-Content -LiteralPath $source -Raw

# The properties this tool has to keep, since it cannot claim to be harmless.
#
# Each is a thing whose absence would turn a controlled experiment into an
# uncontrolled one, and each is cheap to check by name because the source is
# ours and small. A regex cannot prove the restore path runs; it can prove the
# restore path exists and has not been deleted by someone in a hurry.
$required = @(
    @{ Pattern = 'machine_status_word';
       Why = "the virtual-8086 check, without which unreal mode faults" },
    @{ Pattern = 'unreal_self_test';
       Why = "the self-test that stops a broken flat read reading as a dead aperture" },
    @{ Pattern = 'leave_unreal';
       Why = "putting FS's segment limit back" },
    @{ Pattern = 'restore_text_mode';
       Why = "putting the video mode back" },
    @{ Pattern = 'restore_everything';
       Why = "the single exit path that undoes the rest" },
    @{ Pattern = 's3_relock';
       Why = "re-locking the extended registers" },
    @{ Pattern = 'top_of_ram';
       Why = "the guard that stops a flat write landing in system RAM" },
    @{ Pattern = 'NOT for testers';
       Why = "the usage text saying out loud that this is not a survey" }
)
foreach ($rule in $required) {
    if ($sourceText -notmatch [regex]::Escape($rule.Pattern)) {
        throw "vlb_aperture_dos.c is missing $($rule.Why) ('$($rule.Pattern)')."
    }
}

# And one thing it must not become: the survey's distribution. If this binary
# ever lands in the folder handed to testers, the safety argument for that folder
# is void.
$surveyDir = Join-Path $repoRoot "build\vga-survey"
if (Test-Path -LiteralPath (Join-Path $surveyDir "V9XAPER.EXE")) {
    throw "V9XAPER.EXE is in the tester distribution folder. Remove it."
}

$watcomRoot = if ($env:WATCOM) { $env:WATCOM } else { "C:\WATCOM" }
$compiler = @((Join-Path $watcomRoot "binnt64\wcl.exe"),
              (Join-Path $watcomRoot "binnt\wcl.exe")) |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $compiler) { throw "Open Watcom DOS compiler was not found." }
$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = Join-Path $watcomRoot "h"

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir "V9XAPER.EXE"
Push-Location $outputDir
try {
    # -0 pins 8086 code generation. The 386 encodings in this tool are all in
    # inline assembly behind a runtime CPU gate; the compiler itself must not
    # emit any, or the gate would be bypassed before it could run.
    & $compiler "-bt=dos" "-ms" "-zq" "-wx" "-os" "-k4096" "-0" `
        "-i=$(Join-Path $repoRoot 'include')" `
        "-dV9X_BUILD_ID=`"$BuildId`"" "-fe=$exe" $source
    if ($LASTEXITCODE -ne 0) { throw "Failed to build the VLB aperture probe." }
} finally { Pop-Location }

Get-ChildItem -LiteralPath $outputDir -Filter "*.obj" -File | Remove-Item -Force

$bytes = [IO.File]::ReadAllBytes($exe)
if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
    throw "The VLB aperture probe is not an MZ executable."
}
$text = [Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @($BuildId, "Velocity9x VLB linear-aperture probe",
                      "NOT for testers")) {
    if (-not $text.Contains($marker)) {
        throw "VLB aperture probe lacks marker $marker"
    }
}

$readme = @"
VELOCITY9X VLB LINEAR-APERTURE PROBE
Build: $BuildId

THIS IS NOT THE HARDWARE SURVEY. DO NOT SEND IT TO ANYONE.

V9XSURV reads. This writes: it sets a video mode, writes the card's aperture
registers, and enters protected mode briefly to reach addresses above 16 MB.
Run it only on hardware you own and can power-cycle.

It answers one question. On a PCI card the linear framebuffer window is a BAR
and the host bridge routes it. On a 486 with a VESA Local Bus card there is no
host bridge: the window's position is programmed into CR58/CR59/CR5A and the
486 chipset has to decode it. Nothing we have measured says whether it does.


BEFORE YOU START

Press F5 at "Starting MS-DOS" so CONFIG.SYS and AUTOEXEC.BAT are skipped
entirely. No HIMEM, no EMM386. The probe refuses to run in virtual-8086 mode
because unreal mode cannot be entered from it, and it will tell you so.


THE SEQUENCE, LEAST RISKY FIRST

Each step's report is complete on disk before the next one begins. If a step
locks the machine up, power-cycle it, and send the reports that exist along
with which step died.

   1. V9XAPER /out:C:\AP1.INI
      Reads only. Proves unreal mode works by reading the system BIOS ROM two
      different ways and requiring the answers to match, then says what is at
      the address the card's window register holds.

   2. V9XAPER /pattern /out:C:\AP2.INI
      Sets 640x480x8, writes a 32-byte marker into video memory through the
      A0000h window, then looks for that marker at the window's address. A
      match is proof. Absence is only a hint.

   3. V9XAPER /pattern /enable /out:C:\AP3.INI
      Same, with linear addressing switched on in CR58 first.

   4. V9XAPER /pattern /relocate:04000000 /enable /out:C:\AP4.INI
      Same, with the window moved to 64 MB first.

If an S3VBE TSR is available, this is worth doing as well - it lets the TSR
choose the address instead of us, and reports what it chose:

   S3VBE20 /INSTALL
   V9XAPER /pattern /linear /out:C:\AP5.INI


WHAT IT PUTS BACK

The video mode, CR58/CR59/CR5A, CR38/CR39, A20, and FS's segment limit.
Nothing it does survives a reboot in any case.
"@

$hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
foreach ($item in @(@{ Name = "README.TXT"; Body = $readme },
                    @{ Name = "SHA256.TXT"; Body = "$hash  V9XAPER.EXE" })) {
    $target = Join-Path $outputDir $item.Name
    $body = $item.Body -replace "`r`n", "`n" -replace "`n", "`r`n"
    try {
        Set-Content -LiteralPath $target -Encoding Ascii -Value $body
    } catch [System.IO.IOException] {
        Write-Warning "Could not refresh $target; it is open in another process."
    }
}

Write-Output "Built VLB linear-aperture probe: $exe"
Write-Output ("  {0:N0} bytes, SHA256 {1}" -f $bytes.Length, $hash)
Write-Output "  Not for distribution. Bring-up probe for our own hardware."

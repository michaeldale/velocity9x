# Build the standalone VGA hardware survey handed to owners of unsupported cards.
#
# Real-mode DOS rather than Win32, because that is the only place a single
# executable can read PCI configuration space, the video BIOS image and the raw
# VGA register file without a driver. The output folder is the whole
# distribution: the executable plus the instructions the tester needs.
[CmdletBinding()]
param(
    [string]$BuildId,
    # Run the source-safety gate against deliberately broken copies of the
    # source and assert every one is rejected, then stop. Needs no compiler.
    [switch]$GateSelfTest
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\vga-survey"
$source = Join-Path $repoRoot "tools\diag\vga_survey_dos.c"
. (Join-Path $PSScriptRoot "common.ps1")
$productVersion = Get-V9xProductVersion -RepoRoot $repoRoot
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "vga-survey-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw "Invalid BuildId" }

# Source-level safety gate.
#
# The survey goes out to strangers running hardware nobody here can inspect, so
# the claim that it cannot alter their machine has to be enforced rather than
# reviewed. This gate is the reason the tool can be handed to a stranger at all,
# and it grows with the tool.
#
# It is a regex over source text, and it is worth being honest about what that
# can and cannot see. It cannot check that no `outp` reaches a port outside the
# audited set, because most calls take a variable - `outp(index_port, index)` -
# and the port value is simply not in the text. What it *can* enforce, and does:
#
#   * the calls that change hardware state may not appear at all;
#   * every literal port constant at an `inp`/`outp` call site must be one of
#     the audited VGA ports, and every non-literal must be one of the two
#     derived index ports, so a new port cannot be introduced without editing
#     this list;
#   * schema 2 added inline assembly, so the raw opcode bytes it emits are
#     allowlisted too, and the state-changing instructions a pragma could
#     otherwise smuggle in are named and refused.
function Test-V9xSurveySafety {
    param([Parameter(Mandatory = $true)][string]$Text)

    $lower = $Text.ToLowerInvariant()

    $banned = @(
        @{ Pattern = '0x4f02';                 Why = "VBE 4F02h set mode" },
        @{ Pattern = '0x4f05';                 Why = "VBE 4F05h window control" },
        @{ Pattern = '0x4f06';                 Why = "VBE 4F06h scanline length" },
        @{ Pattern = '0x4f07';                 Why = "VBE 4F07h display start" },
        @{ Pattern = '0x4f08';                 Why = "VBE 4F08h DAC width" },
        @{ Pattern = '0x4f09';                 Why = "VBE 4F09h palette data" },
        @{ Pattern = '0x4f0b';                 Why = "VBE 4F0Bh pixel clock" },
        @{ Pattern = '0x4f14';                 Why = "VBE 4F14h OEM extension" },
        @{ Pattern = '0xb10b|0xb10c|0xb10d';   Why = "PCI BIOS config-space write" },
        @{ Pattern = '0x0?cf8|0x0?cfc';        Why = "direct PCI config port access" },
        # These match an inline-asm operand list, not merely a string that
        # starts with the same letters: the whole quoted string has to be the
        # mnemonic and its operands, so the tool's own /out: switch is not
        # mistaken for an OUT instruction.
        @{ Pattern = '"\s*(lmsw|lgdt|lidt|wrmsr|invd|wbinvd|out|outs[bwd]?)(\s[^"]*)?"';
           Why = "inline assembly that changes machine state" },
        @{ Pattern = '"\s*mov\s+(cr|dr)\d[^"]*"';
           Why = "inline control-register write" }
    )
    foreach ($rule in $banned) {
        if ($lower -match $rule.Pattern) {
            throw "vga_survey_dos.c contains $($rule.Why); the survey must not."
        }
    }

    # The audited port set. Everything the tool touches is in the VGA block at
    # 3B0-3DF; index_port and status_port are the two derived from the MISC
    # output register's colour/mono bit.
    $allowedPorts = @(
        '0x03b4', '0x03ba', '0x03c0', '0x03c1', '0x03c2', '0x03c4', '0x03c5',
        '0x03c6', '0x03c7', '0x03c8', '0x03ca', '0x03cc', '0x03ce', '0x03d4',
        '0x03da', 'index_port', 'status_port'
    )
    $callSites = [regex]::Matches($lower, '\b(?:inp|outp)\s*\(\s*([^,()]+)')
    if ($callSites.Count -eq 0) {
        throw "The port-constant gate found no inp/outp call sites; it is not looking at the source it thinks it is."
    }
    foreach ($site in $callSites) {
        $token = $site.Groups[1].Value -replace '\s', ''
        $token = $token -replace '\+1u?$', ''
        $token = $token -replace 'u$', ''
        if ($allowedPorts -notcontains $token) {
            throw "vga_survey_dos.c does port I/O on '$token', which is not in the audited port set."
        }
    }

    # Software interrupts issued from inline assembly. Only the extended-memory
    # block move is reached that way; everything else goes through int86/int86x,
    # where the vector is a C argument and reviewable as one. The operand is
    # extracted and compared rather than pattern-matched, because a negative
    # lookahead over '\s+' backtracks and would pass anything.
    $allowedInterrupts = @('15h')
    foreach ($site in [regex]::Matches($lower, '"\s*int\s+([^"]*)"')) {
        $vector = $site.Groups[1].Value.Trim()
        if ($allowedInterrupts -notcontains $vector) {
            throw "vga_survey_dos.c issues INT $vector from inline assembly, which is not in the audited set."
        }
    }

    # Raw opcode bytes emitted from a #pragma aux. Open Watcom's inline
    # assembler refuses SMSW at every CPU setting it offers for a 16-bit DOS
    # target, so that one encoding is spelled out by hand - and being spelled out
    # by hand is exactly why it has to be allowlisted.
    $allowedBytes = @('0x0f,0x01,0xe0')
    foreach ($site in [regex]::Matches($lower, '"\s*db\s+([^"]*)"')) {
        $bytes = $site.Groups[1].Value -replace '\s', ''
        if ($allowedBytes -notcontains $bytes) {
            throw "vga_survey_dos.c emits the raw opcode bytes '$bytes', which are not in the audited set."
        }
    }
}

$sourceText = Get-Content -LiteralPath $source -Raw
Test-V9xSurveySafety -Text $sourceText

# The gate has to be shown to reject, not just to pass. Each mutation below is a
# thing the tool must never grow, and -GateSelfTest asserts every one is caught.
if ($GateSelfTest) {
    $mutations = @(
        @{ Name = "VBE set mode";        Add = 'static unsigned short bad = 0x4f02u;' },
        @{ Name = "PCI config write";    Add = 'static unsigned short bad = 0xb10bu;' },
        @{ Name = "direct PCI port";     Add = 'static unsigned short bad = 0x0cf8u;' },
        @{ Name = "unaudited port";      Add = 'static void bad(void) { outp(0x0060u, 0u); }' },
        @{ Name = "unaudited port read"; Add = 'static int bad(void) { return inp(0x1ce); }' },
        @{ Name = "unallowlisted bytes"; Add = 'static void bad(void); #pragma aux bad = "db 0x0f, 0x22, 0xc0";' },
        @{ Name = "descriptor load";     Add = 'static void bad(void); #pragma aux bad = "lgdt [si]";' },
        @{ Name = "port write in asm";   Add = 'static void bad(void); #pragma aux bad = "out dx,al";' },
        @{ Name = "inline INT 10h";      Add = 'static void bad(void); #pragma aux bad = "int 10h";' }
    )
    $failures = @()
    foreach ($mutation in $mutations) {
        $rejected = $false
        try {
            Test-V9xSurveySafety -Text ($sourceText + "`n" + $mutation.Add + "`n")
        } catch {
            $rejected = $true
        }
        if ($rejected) {
            Write-Output "  rejected: $($mutation.Name)"
        } else {
            $failures += $mutation.Name
        }
    }
    if ($failures.Count -ne 0) {
        throw ("The survey safety gate accepted: " + ($failures -join ', '))
    }
    Write-Output ("VGA survey safety gate self-test passed ({0} mutations rejected, clean source accepted)." -f
                  $mutations.Count)
    return
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
$exe = Join-Path $outputDir "V9XSURV.EXE"
Push-Location $outputDir
try {
    & $compiler "-bt=dos" "-ms" "-zq" "-wx" "-os" "-k4096" "-0" `
        "-dV9X_BUILD_ID=`"$BuildId`"" "-fe=$exe" $source
    if ($LASTEXITCODE -ne 0) { throw "Failed to build the VGA survey." }
} finally { Pop-Location }

# wcl leaves its object file in the working directory, and this directory is
# what gets handed to a tester - it holds the distribution, not build leftovers.
Get-ChildItem -LiteralPath $outputDir -Filter "*.obj" -File |
    Remove-Item -Force

$bytes = [IO.File]::ReadAllBytes($exe)
if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
    throw "The VGA survey is not an MZ executable."
}
$text = [Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @($BuildId, "Velocity9x VGA hardware survey", "query-only")) {
    if (-not $text.Contains($marker)) { throw "VGA survey lacks marker $marker" }
}

$readme = @"
VELOCITY9X VGA HARDWARE SURVEY
Build: $BuildId ($productVersion)

WHAT THIS IS

Velocity9x is a display driver for Windows 98. It currently supports only a
couple of S3 chips. To support your card we need to know what is actually on
it, and this program collects that: the PCI identifiers, the video BIOS, the
video modes the card advertises, your monitor's EDID, and the VGA registers.

It reads. It does not change your video mode, does not install anything, and
does not leave anything behind on the card. The only file it creates is the
report.


HOW TO RUN IT

Best results come from real DOS, not a DOS window inside Windows.

   1. In Windows 98: Start, Shut Down, "Restart in MS-DOS mode".
      (Or boot from a DOS floppy. Or if the machine only runs DOS, you are
      already there.)
   2. Change to the folder holding V9XSURV.EXE.
   3. Type:  V9XSURV
   4. Answer the one question it asks (see below).
   5. Send back the file it names at the end - normally C:\V9XSURV.INI

Running it from a DOS box inside Windows does work and still produces a
useful file. It just cannot see as much.


THE QUESTION IT ASKS

Partway through it asks whether to run the vendor-specific probe. That step
writes the unlock keys published for your chipset family, reads the registers
behind them, and puts the originals back. It is where the video memory size,
the clock settings and the aperture layout come from, so please say yes if
you can.

Saying no is fine. The main report is already saved to disk before the
question is asked, so you lose only that last section.

If Windows is running, it will recommend saying no. Take its advice.


OPTIONS

   V9XSURV /rom        also dump the complete video BIOS image. Makes the
                       report several times larger, but is the best way to
                       identify an unusual card. Please use this if asked.
   V9XSURV /tier2      say yes to the question without being asked
   V9XSURV /notier2    say no to the question without being asked
   V9XSURV /aperture   read a few bytes from the card's linear memory
                       window, to find out whether the machine decodes
                       addresses there at all. Only asked for on ISA and
                       VESA Local Bus cards. See below.
   V9XSURV /out:A:\V9XSURV.INI    write the report somewhere else, for
                       example to a floppy on a machine with no spare disk
   V9XSURV /?          show this list


IF THE CARD IS ISA OR VESA LOCAL BUS

There is no PCI on those machines, so nothing can identify the card by
scanning the bus. Instead the vendor probe reads the chip's own identity
registers and continues only if they already say the card is an S3. Say yes
to the question, and please also put the markings on the card and its main
chip into a plain text file called V9XNOTE.TXT next to V9XSURV.EXE - that is
the one thing no register can tell us.

If we have asked for /aperture, run it like this:

   1. Reboot, hold F8, and choose "Command prompt only". No CONFIG.SYS, no
      HIMEM, no EMM386. The reading is more trustworthy without them.
   2. V9XSURV /aperture /rom /out:C:\V9XAPER.INI
   3. Send both reports - the ordinary one and this one.

Keep the reports separate and keep them all. If a later run locks the
machine up, the earlier reports are still on disk and still useful.

/aperture reads memory. It does not write to memory and does not write to
the card.


WHAT IS IN THE FILE

Plain text - open it in any editor and read it before you send it. It holds
hardware identifiers and register values only: no filenames, no serial
numbers from your machine, no personal information. It also records how much
memory is installed, which CPU is fitted, and whether HIMEM or EMM386 is
loaded, because on an older machine those decide where a card's memory
window can live. The one identifier that belongs to a physical object is
your monitor's EDID, which carries the monitor's model and its factory
serial number.


IF SOMETHING GOES WRONG

If the machine stops responding, power-cycle it. Nothing the survey does
survives a reboot. Then re-run with:

   V9XSURV /notier2

and send that report along with a note saying which card is fitted - the
basic report is still worth having, and knowing which card wedged the vendor
probe is itself useful.
"@

# The executable is the build's actual output. A reader holding one of these
# generated text files open - an editor, a file-transfer pane, a mounted folder
# CD - must not fail a build that has already produced and verified the binary.
$hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$generated = @{
    "README.TXT" = ($readme -replace "`r`n", "`n" -replace "`n", "`r`n")
    "SHA256.TXT" = "$hash  V9XSURV.EXE"
}
foreach ($name in $generated.Keys) {
    $target = Join-Path $outputDir $name
    try {
        Set-Content -LiteralPath $target -Encoding Ascii -Value $generated[$name]
    } catch [System.IO.IOException] {
        Write-Warning "Could not refresh $target; it is open in another process."
    }
}

Write-Output "Built VGA hardware survey: $exe"
Write-Output ("  {0:N0} bytes, SHA256 {1}" -f $bytes.Length, $hash)
Write-Output "  Distribution folder: $outputDir"

# Read V9XSURV.INI reports sent back by testers and turn them into something
# actionable.
#
# The DOS tool deliberately captures rather than interprets, so all the decoding
# lives here: EDID, PCI BARs, the ROM image. That split means a decoding mistake
# is fixed by editing this file and re-running it over every report already
# collected, instead of shipping a new executable to everyone who helped.
[CmdletBinding()]
param(
    # One report, or a folder of them.
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Path,
    # Append a normalised one-line-per-card record here.
    [string]$Csv,
    # Reconstruct the captured video BIOS image to this file (single report only).
    [string]$ExtractRom,
    # Reconstruct the captured EDID block (single report only).
    [string]$ExtractEdid,
    [switch]$Detailed
)

$ErrorActionPreference = "Stop"

function Read-IniFile {
    param([string]$LiteralPath)

    $data = [ordered]@{}
    $section = "(root)"
    $data[$section] = [ordered]@{}
    foreach ($line in [IO.File]::ReadAllLines($LiteralPath)) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith(";")) { continue }
        if ($trimmed.StartsWith("[") -and $trimmed.EndsWith("]")) {
            $section = $trimmed.Substring(1, $trimmed.Length - 2)
            if (-not $data.Contains($section)) { $data[$section] = [ordered]@{} }
            continue
        }
        # Split on the first '=' only: extracted BIOS strings can contain more.
        $split = $trimmed.IndexOf("=")
        if ($split -lt 1) { continue }
        $key = $trimmed.Substring(0, $split)
        $value = $trimmed.Substring($split + 1)
        $data[$section][$key] = $value
    }
    return $data
}

function Get-IniValue {
    param($Ini, [string]$Section, [string]$Key, $Default = $null)
    if (-not $Ini.Contains($Section)) { return $Default }
    if (-not $Ini[$Section].Contains($Key)) { return $Default }
    return $Ini[$Section][$Key]
}

# Blob sections are offset-keyed hex lines. One rule reassembles config space,
# the ROM image and EDID alike.
#
# -Base is for the schema-2 register banks, whose offsets are register indices
# rather than positions in a buffer: Chipset.S3/CrtcUnlocked starts at CR30, so
# -Base 0x30 returns a byte array whose element 0 is CR30.
function Get-Blob {
    param($Ini, [string]$Section, [string]$Prefix, [int]$Base = 0)

    if (-not $Ini.Contains($Section)) { return $null }
    $chunks = @{}
    foreach ($key in $Ini[$Section].Keys) {
        if ($key -notlike "$Prefix.*") { continue }
        $offsetText = $key.Substring($Prefix.Length + 1)
        $offset = [Convert]::ToInt32($offsetText, 16) - $Base
        if ($offset -lt 0) { continue }
        $chunks[$offset] = $Ini[$Section][$key]
    }
    if ($chunks.Count -eq 0) { return $null }

    $bytes = New-Object System.Collections.Generic.List[byte]
    foreach ($offset in ($chunks.Keys | Sort-Object)) {
        $hex = $chunks[$offset] -replace '[^0-9A-Fa-f]', ''
        if ($bytes.Count -ne $offset) {
            Write-Warning "$Section/$Prefix has a gap or overlap at offset $offset"
        }
        for ($i = 0; $i + 1 -lt $hex.Length; $i += 2) {
            $bytes.Add([Convert]::ToByte($hex.Substring($i, 2), 16))
        }
    }
    return $bytes.ToArray()
}

function ConvertFrom-Edid {
    param([byte[]]$Bytes)

    if ($null -eq $Bytes -or $Bytes.Length -lt 128) { return $null }
    $header = @(0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00)
    for ($i = 0; $i -lt 8; $i++) {
        if ($Bytes[$i] -ne $header[$i]) { return [pscustomobject]@{ Valid = $false } }
    }
    $sum = 0
    for ($i = 0; $i -lt 128; $i++) { $sum = ($sum + $Bytes[$i]) -band 0xFF }

    # Every byte is widened to [int] before shifting. Windows PowerShell 5.1
    # keeps a shift inside the width of its left operand, so [byte]0x4D -shl 8
    # is 0, not 0x4D00 - which silently corrupts every multi-byte field.
    # Bytes 8-9 pack three letters into five bits each, 1 = 'A'.
    $packed = ([int]$Bytes[8] -shl 8) -bor [int]$Bytes[9]
    $vendor = ""
    foreach ($shift in 10, 5, 0) {
        $vendor += [char]((($packed -shr $shift) -band 0x1F) + 64)
    }

    # The first detailed timing descriptor is the monitor's preferred mode.
    $d = 54
    $preferred = $null
    if ($Bytes[$d] -ne 0 -or $Bytes[$d + 1] -ne 0) {
        $clock = ((([int]$Bytes[$d + 1] -shl 8) -bor [int]$Bytes[$d])) * 10
        $hActive = [int]$Bytes[$d + 2] -bor
                   (((([int]$Bytes[$d + 4] -shr 4) -band 0x0F)) -shl 8)
        $vActive = [int]$Bytes[$d + 5] -bor
                   (((([int]$Bytes[$d + 7] -shr 4) -band 0x0F)) -shl 8)
        $preferred = "{0}x{1} @ {2:N2} MHz" -f $hActive, $vActive, ($clock / 1000)
    }

    # Descriptors tagged FCh carry the monitor's model name.
    $name = ""
    foreach ($base in 54, 72, 90, 108) {
        if ($Bytes[$base] -eq 0 -and $Bytes[$base + 1] -eq 0 -and
            $Bytes[$base + 3] -eq 0xFC) {
            $raw = [Text.Encoding]::ASCII.GetString($Bytes, $base + 5, 13)
            $name = ($raw -split "`n")[0].Trim()
        }
    }

    return [pscustomobject]@{
        Valid        = $true
        ChecksumOk   = ($sum -eq 0)
        Manufacturer = $vendor
        ProductCode  = "{0:X4}" -f (([int]$Bytes[11] -shl 8) -bor [int]$Bytes[10])
        Serial       = "{0:X8}" -f ([BitConverter]::ToUInt32($Bytes, 12))
        Week         = $Bytes[16]
        Year         = 1990 + $Bytes[17]
        Version      = "$($Bytes[18]).$($Bytes[19])"
        MonitorName  = $name
        Preferred    = $preferred
        Extensions   = $Bytes[126]
    }
}

function ConvertFrom-Bar {
    param([string]$Raw)

    if ([string]::IsNullOrWhiteSpace($Raw)) { return $null }
    # [int64] throughout: a 32-bit BAR with the top bit set is negative as an
    # Int32, which turns the masks below into nonsense.
    $value = [int64][Convert]::ToUInt32($Raw, 16)
    if ($value -eq 0) { return $null }
    if (($value -band 1) -ne 0) {
        return [pscustomobject]@{
            Space = "io"; Base = "{0:X8}" -f ($value -band 0xFFFFFFFCL)
            Prefetchable = $false; Type = "32"
        }
    }
    $type = switch (($value -shr 1) -band 0x3) {
        0 { "32" } 2 { "64" } default { "reserved" }
    }
    return [pscustomobject]@{
        Space        = "memory"
        Base         = "{0:X8}" -f ($value -band 0xFFFFFFF0L)
        Prefetchable = ((($value -shr 3) -band 1) -ne 0)
        Type         = $type
    }
}

function Get-HexValue {
    param([string]$Raw, $Default = $null)

    if ([string]::IsNullOrWhiteSpace($Raw)) { return $Default }
    try { return [int64][Convert]::ToUInt64($Raw.Trim(), 16) } catch { return $Default }
}

# ---------------------------------------------------------------------------
# S3 chip identity
#
# CR2D/CR2E hold the same device id the PCI parts publish to configuration
# space, so this table is keyed on the same ids the driver's own family
# manifest is - and that mechanism is as solid as the PCI ids themselves.
#
# The names attached to those ids are a separate question, and were previously
# covered by the same claim when they should not have been. These thirteen were
# read out of the PCIR structure of an option ROM dump, which is a first-party
# statement by the board the dump came from:
#
#   5631 883D 8811 8880 88B0 88C0 88C1 88D0 88F0 8901 8A01 8A10 8A13
#
# See docs\decisions\2026-08-29-s3-device-id-survey.md and
# scripts\read-option-rom-ids.ps1, which reproduces it. The rest of the table
# comes from the public PCI id list. That survey corrected two entries that had
# been documentation all along: 86C765 sits at 8811, not 8814.
#
# CR30 is different, and worth being honest about. It is the only id register
# the pre-Trio parts have - the 86C801/805 and 86C928 that a VLB card is likely
# to be - and this project has never seen one. The names below come from
# documentation, not from measurement, so a chip named only from CR30 is
# reported as a lead and labelled as one. Being wrong here costs a script edit.
# ---------------------------------------------------------------------------
$script:S3DeviceIds = @{
    '5631' = 'ViRGE (86C325)'
    '883D' = 'ViRGE/VX (86C988)'
    '8810' = 'Trio32 (86C732)'
    # Measured: the S3 Trio64V+ reference BIOS and a Trio32 BIOS both publish
    # 8811, alongside seven Trio64 board ROMs. 86C765 was previously listed at
    # 8814 instead, which the Trio64V+ dump contradicts.
    '8811' = 'Trio32, Trio64 or Trio64V+ (86C732/86C764/86C765)'
    '8812' = 'Trio64V+ family / Aurora64V+ (86C862)'
    '8813' = 'Trio32 or Trio64 (86C732/86C764)'
    '8814' = 'Trio64UV+ (86C767)'
    '8880' = 'Vision868 (86C868)'
    '88B0' = 'Vision928 (86C928)'
    '88B1' = 'Vision928 (86C928)'
    '88C0' = 'Vision864 (86C864)'
    '88C1' = 'Vision864 (86C864)'
    '88D0' = 'Vision964 (86C964)'
    '88D1' = 'Vision964 (86C964)'
    '88F0' = 'Vision968 (86C968)'
    '88F1' = 'Vision968 (86C968)'
    '8901' = 'Trio64V2/DX or /GX (86C775/86C785)'
    '8902' = 'Plato/PX (86C551)'
    '8903' = 'Trio3D (86C365)'
    '8904' = 'Trio3D/2X (86C362)'
    '8A01' = 'ViRGE/DX or /GX (86C375/86C385)'
    '8A10' = 'ViRGE/GX2 (86C357)'
    '8A13' = 'Trio3D/2X (86C362/86C368)'
    '8A20' = 'Savage3D'
    '8A22' = 'Savage4'
    '8C00' = 'ViRGE/MX (86C260)'
    '8C01' = 'ViRGE/MX (86C260)'
    '8C03' = 'ViRGE/MX+ (86C280)'
    '8C10' = 'Savage/MX'
    '9102' = 'Savage2000'
}

$script:S3ChipIds = @{
    0x81 = '86C911'
    0x82 = '86C911-A or 86C924'
    0x90 = '86C928'
    0x91 = '86C928'
    0x92 = '86C928'
    0x94 = '86C928'
    0x95 = '86C928'
    0xA0 = '86C801 or 86C805'
    0xA2 = '86C801 or 86C805'
    0xA5 = '86C801 or 86C805'
    0xA8 = '86C801 or 86C805'
    0xB0 = '86C928'
    0xC0 = 'Vision864 (86C864)'
    0xC1 = 'Vision864 (86C864)'
    0xD0 = 'Vision964 (86C964)'
    0xD1 = 'Vision964 (86C964)'
    0xE0 = 'Trio32 or Trio64 (86C732/86C764)'
    0xE1 = 'Trio64V+ or Trio64UV+ (86C765)'
    0xF0 = 'Vision968 or Trio64V2'
}

function Get-S3ChipName {
    param([string]$DeviceIdHigh, [string]$DeviceIdLow, [string]$ChipId,
          [string]$Source)

    $high = Get-HexValue $DeviceIdHigh
    $low = Get-HexValue $DeviceIdLow
    $chip = Get-HexValue $ChipId

    if ($null -ne $high -and $null -ne $low) {
        $key = "{0:X2}{1:X2}" -f $high, $low
        if ($script:S3DeviceIds.ContainsKey($key)) {
            return [pscustomobject]@{
                Name = $script:S3DeviceIds[$key]; Key = $key
                From = "CR2D/CR2E"; Source = $Source; Confident = $true
            }
        }
        if ($high -ne 0xFF -and $high -ne 0x00) {
            return [pscustomobject]@{
                Name = "unknown S3 device id $key"; Key = $key
                From = "CR2D/CR2E"; Source = $Source; Confident = $false
            }
        }
    }
    if ($null -ne $chip -and $script:S3ChipIds.ContainsKey([int]$chip)) {
        return [pscustomobject]@{
            Name = $script:S3ChipIds[[int]$chip]
            Key = "{0:X2}" -f $chip
            From = "CR30"; Source = $Source; Confident = $false
        }
    }
    return $null
}

# CR58, the linear address window control. Bits 1-0 are the window size and bit
# 4 enables linear addressing - the same fields src\chipsets\s3\common\
# s3_regs16.c writes when it selects a 4 MiB window. Bits 7-5 differ between
# parts, so they are reported raw rather than named.
function ConvertFrom-S3Cr58 {
    param([string]$Raw)

    $value = Get-HexValue $Raw
    if ($null -eq $value) { return $null }
    $sizes = @(65536L, 1048576L, 2097152L, 4194304L)
    return [pscustomobject]@{
        Raw            = "{0:X2}" -f $value
        WindowSize     = $sizes[[int]($value -band 0x3)]
        LinearEnabled  = ((($value -shr 4) -band 1) -ne 0)
        UpperBits      = "{0:X2}" -f ($value -band 0xE0)
    }
}

# CR36 bits 7-5, using the same code table as
# v9x_s3_virge_decode_memory_size in src\chipsets\s3\virge\memory.c, so the
# survey and the driver cannot disagree about what a code means.
function ConvertFrom-S3Cr36 {
    param([string]$Raw)

    $value = Get-HexValue $Raw
    if ($null -eq $value) { return $null }
    $code = ($value -shr 5) -band 0x7
    $bytes = switch ($code) {
        0 { 4194304L } 3 { 8388608L } 4 { 2097152L } 6 { 1048576L }
        7 { 524288L } default { $null }
    }
    return [pscustomobject]@{
        Raw = "{0:X2}" -f $value; Code = $code; Bytes = $bytes
    }
}

# ---------------------------------------------------------------------------
# The bus.
#
# Schema 2 adds no [Bus] section, because the report already holds the facts:
# [PciBios] records whether INT 1Ah AX=B101h answered, and [PciInventory]
# counts the display-class devices the walk found. Drawing the conclusion is
# this script's job, which is the whole point of the capture/interpret split -
# a schema-1 report from an ISA card yields the same verdict without having been
# re-collected.
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Who is answering VBE.
#
# Measured on the 486 VLB machine of 2026-08-21, which reported VBE 2.00 with a
# linear framebuffer from a card whose own ROM offers neither: an S3VBE 3.18 TSR
# was resident. The tell is in a field the report already carried.
# `VideoModePtr` is a far pointer, and the card's own BIOS returns one into its
# own option ROM - `C000534F` there. Under the TSR the same key read `0DC62612`,
# a low-RAM address.
#
# So the segment of that pointer says whose VBE this is, and everything the [VBE]
# and [VBEModes] sections describe belongs to whoever it names - which decides
# whether a linear-framebuffer attribute is a fact about the card or a promise
# made by software. `Int10Vector` says the same thing and was added for it, but
# this works on every schema-2 report including those taken before it existed.
# ---------------------------------------------------------------------------
function Get-VbeProvider {
    param($Ini)

    $pointer = Get-HexValue (Get-IniValue $Ini "VBEModes" "ModeListPointer")
    if ($null -eq $pointer -or $pointer -eq 0) { return $null }
    $segment = ($pointer -shr 16) -band 0xFFFF
    if ($segment -ge 0xC000 -and $segment -le 0xC7FF) {
        return [pscustomobject]@{
            Owner = "the card's option ROM"; Segment = "{0:X4}" -f $segment
            IsRom = $true
        }
    }
    return [pscustomobject]@{
        Owner = "something other than the card's ROM (a TSR or a shadow copy)"
        Segment = "{0:X4}" -f $segment
        IsRom = $false
    }
}

function Get-BusVerdict {
    param($Ini)

    if ((Get-IniValue $Ini "PciBios" "Status") -ne "ok") {
        $reason = Get-IniValue $Ini "PciBios" "Reason" "unknown"
        return "non-pci (no PCI BIOS: $reason)"
    }
    $count = [int](Get-IniValue $Ini "PciInventory" "DisplayDeviceCount" "0")
    if ($count -eq 0) {
        return "pci-bios-present-but-no-pci-display-device (ISA or VLB card)"
    }
    return "pci"
}

# ---------------------------------------------------------------------------
# Top of installed RAM.
#
# Needed for one reason: an aperture window whose base sits at or below it reads
# back RAM, which looks alive and proves nothing about the card. The three
# sources are tried best-first and which one answered is reported, because a
# BIOS that caps AH=88h at 15 MB would otherwise make a 16 MB machine look
# small enough to clear a window that in fact overlaps it.
# ---------------------------------------------------------------------------
function Get-InstalledRam {
    param($Ini)

    $unknown = [pscustomobject]@{ Bytes = $null; Source = "unknown" }
    if (-not $Ini.Contains("Platform")) { return $unknown }

    if ((Get-IniValue $Ini "Platform" "Int15E820Status") -eq "ok") {
        $top = 0L
        foreach ($key in $Ini["Platform"].Keys) {
            if ($key -notlike "E820.*") { continue }
            $fields = $Ini["Platform"][$key] -split ","
            if ($fields.Count -lt 3) { continue }
            if ([int](Get-HexValue $fields[2] 0) -ne 1) { continue }
            $base = Get-HexValue $fields[0] 0
            $length = Get-HexValue $fields[1] 0
            if (($base + $length) -gt $top) { $top = $base + $length }
        }
        if ($top -gt 0) {
            return [pscustomobject]@{ Bytes = $top; Source = "INT 15h E820h" }
        }
    }

    if ((Get-IniValue $Ini "Platform" "Int15E801Status") -eq "ok") {
        $kb = [int64](Get-IniValue $Ini "Platform" "Int15E801ConfiguredKB" "0")
        $blocks = [int64](Get-IniValue $Ini "Platform" "Int15E801Configured64KB" "0")
        if ($blocks -gt 0) {
            return [pscustomobject]@{
                Bytes = 16777216L + ($blocks * 65536L); Source = "INT 15h E801h"
            }
        }
        if ($kb -gt 0) {
            return [pscustomobject]@{
                Bytes = 1048576L + ($kb * 1024L); Source = "INT 15h E801h"
            }
        }
    }

    if ((Get-IniValue $Ini "Platform" "Int1588Status") -eq "ok") {
        $kb = [int64](Get-IniValue $Ini "Platform" "Int1588ExtendedKB" "0")
        # Measured on the 486 VLB run of 2026-08-21: with EMM386 resident,
        # AH=88h answers 0 KB, because a memory manager hides extended memory
        # from the call that other allocators would use to claim it. Returning
        # "1 MB installed" from that is not a small error - it is the figure the
        # aperture false-positive check compares a window base against, so a
        # wrong small answer silently disables the check. Nothing is better.
        if ($kb -eq 0) {
            $why = "INT 15h AH=88h answered 0 KB"
            if ((Get-IniValue $Ini "Platform" "EmsPresent") -eq "yes" -or
                (Get-IniValue $Ini "Platform" "XmsPresent") -eq "yes") {
                $why += " with a memory manager resident, which intercepts it"
            }
            return [pscustomobject]@{ Bytes = $null; Source = $why }
        }
        return [pscustomobject]@{
            Bytes = 1048576L + ($kb * 1024L)
            Source = "INT 15h AH=88h (capped at 15 MB on many BIOSes)"
        }
    }
    return $unknown
}

# ---------------------------------------------------------------------------
# The aperture verdict.
#
# The tool reports bytes; deciding what they mean belongs here, and most of that
# decision is about refusing to over-read a positive. Three ways a live-looking
# result means nothing:
#
#   the window base overlaps installed RAM, so AH=87h returned RAM;
#   a memory manager emulated AH=87h instead of the BIOS executing it;
#   no mode was ever set, so a dead window may simply not be switched on yet.
#
# The first is disqualifying and produces unreadable-by-this-method. The other
# two are caveats attached to whatever the verdict is.
# ---------------------------------------------------------------------------
function Get-ApertureVerdict {
    param($Ini, $Ram)

    if (-not $Ini.Contains("Aperture")) {
        return [pscustomobject]@{
            Verdict = "absent"; Detail = "no aperture probe in this report"
            Caveats = @()
        }
    }

    $status = Get-IniValue $Ini "Aperture" "Status"
    $reason = Get-IniValue $Ini "Aperture" "Reason"
    $caveats = @()

    if ((Get-IniValue $Ini "Aperture" "ProtectedOrV86") -eq "yes" -or
        (Get-IniValue $Ini "Aperture" "EmsPresent") -eq "yes") {
        $caveats += ("the CPU was in virtual-8086 mode, so INT 15h AH=87h was " +
                     "emulated by a memory manager rather than executed by the " +
                     "BIOS; re-run from a clean boot to confirm")
    }

    if ($status -ne "ok") {
        $verdict = "not-measured"
        if ($reason -eq "aperture-switch-not-given") { $verdict = "not-requested" }
        $detail = $status
        if ($reason) { $detail = "$status ($reason)" }
        return [pscustomobject]@{
            Verdict = $verdict; Detail = $detail; Caveats = $caveats
        }
    }

    $base = Get-HexValue (Get-IniValue $Ini "Aperture" "Base")
    if ($null -eq $Ram.Bytes) {
        # The sub-RAM test is the one that turns a false positive into a
        # negative. Without a RAM figure it cannot run, and saying so is the
        # difference between an unchecked result and a checked one.
        $caveats += ("the window base could not be compared against installed " +
                     "RAM, because that is unknown ($($Ram.Source)) - a " +
                     "positive result here has NOT been checked for reading " +
                     "RAM instead of the card")
    }
    if ($null -ne $Ram.Bytes -and $null -ne $base -and $base -lt $Ram.Bytes) {
        return [pscustomobject]@{
            Verdict = "unreadable-by-this-method"
            Detail  = ("window base {0:X8} is at or below the top of installed " +
                       "RAM ({1:N0} bytes, per {2}), so the bytes read are RAM " +
                       "and say nothing about the card") -f
                      $base, $Ram.Bytes, $Ram.Source
            Caveats = $caveats
        }
    }

    $data = Get-Blob $Ini "Aperture" "Data"
    if ($null -eq $data -or $data.Length -eq 0) {
        return [pscustomobject]@{
            Verdict = "inconclusive"; Detail = "the probe reported ok but no data"
            Caveats = $caveats
        }
    }

    $distinct = @($data | Sort-Object -Unique)
    if ($distinct.Count -eq 1 -and ($distinct[0] -eq 0xFF -or $distinct[0] -eq 0x00)) {
        $caveats += ("no mode was set, and on these parts the window may only " +
                     "answer once a mode has been set with linear addressing " +
                     "enabled, so this is suggestive and not conclusive")
        return [pscustomobject]@{
            Verdict = "nothing-decodes"
            Detail  = ("all {0} bytes read back as {1:X2} from base {2:X8}" -f
                       $data.Length, $distinct[0], $base)
            Caveats = $caveats
        }
    }

    return [pscustomobject]@{
        Verdict = "window-responds"
        Detail  = ("{0} bytes from base {1:X8} hold {2} distinct values" -f
                   $data.Length, $base, $distinct.Count)
        Caveats = $caveats
    }
}

# Where the locked Tier 1 dump and the unlocked Tier 2 dump disagree over the
# CR30-CR3F range they both cover. A disagreement is not an error - it is the
# finding that this part's locks gate reads as well as writes, which is exactly
# what the no-PCI identification depends on not being true.
function Compare-CrtcBanks {
    param($Ini)

    $locked = Get-Blob $Ini "VGARegisters" "Crtc"
    $unlocked = Get-Blob $Ini "Chipset.S3" "CrtcUnlocked" -Base 0x30
    if ($null -eq $locked -or $null -eq $unlocked) { return @() }

    $differences = @()
    for ($index = 0x30; $index -le 0x3F; $index++) {
        if ($index -ge $locked.Length) { break }
        if (($index - 0x30) -ge $unlocked.Length) { break }
        if ($locked[$index] -ne $unlocked[$index - 0x30]) {
            $differences += "CR{0:X2} {1:X2}->{2:X2}" -f
                $index, $locked[$index], $unlocked[$index - 0x30]
        }
    }
    return $differences
}

function Read-SurveyReport {
    param([string]$LiteralPath)

    $ini = Read-IniFile -LiteralPath $LiteralPath
    $problems = @()

    if ((Get-IniValue $ini "Report" "Tool") -ne "V9XSURV") {
        throw "$LiteralPath is not a Velocity9x VGA survey report."
    }
    $schema = Get-IniValue $ini "Report" "SchemaVersion"
    if (@("1", "2") -notcontains $schema) {
        $problems += "unknown schema version '$schema'"
    }
    # Complete is the last key the tool writes. Without it the file was cut off
    # in transit and nothing in it should be trusted wholesale.
    if ((Get-IniValue $ini "Result" "Complete") -ne "yes") {
        $problems += "truncated report (no Result/Complete marker)"
    }
    if ((Get-IniValue $ini "VGARegisters" "Trust") -eq "virtualized") {
        $problems += "VGA registers captured under Windows; not hardware values"
    }
    # A V86 host is not the same problem as Windows - EMM386 does not trap the
    # VGA ports, so the register dump is still the chip's - but it demonstrably
    # does trap INT 15h, so the memory figures and the aperture probe's path are
    # its, not the BIOS's. Worth saying, without crying wolf about the registers.
    if ((Get-IniValue $ini "Platform" "ProtectedOrV86") -eq "yes" -and
        (Get-IniValue $ini "System" "WindowsPresent") -ne "yes") {
        $problems += ("captured in virtual-8086 mode under a memory manager: " +
                      "the register reads are still the chip's, but every INT " +
                      "service in this report went through the manager first")
    }
    if ((Get-IniValue $ini "Tier2" "Requested") -ne "yes") {
        $problems += "vendor probe declined; no chipset register detail"
    }
    # A no-PCI report that could not name the card from its registers either is
    # the one outcome that needs a follow-up rather than a decode.
    if ((Get-IniValue $ini "Chipset.Identify" "Accepted") -eq "no") {
        $problems += ("no PCI and the card's own registers did not identify it " +
                      "(CR2D=" + (Get-IniValue $ini "Chipset.Identify" "LockedCR2D" "??") +
                      " CR2E=" + (Get-IniValue $ini "Chipset.Identify" "LockedCR2E" "??") +
                      " CR30=" + (Get-IniValue $ini "Chipset.Identify" "LockedCR30" "??") + ")")
    }

    # The first display-class device is the card of interest.
    $device = $null
    if ($ini.Contains("PciDevice.0")) { $device = $ini["PciDevice.0"] }

    $edid = ConvertFrom-Edid (Get-Blob $ini "EDID" "Block0")

    $vendorId = if ($device) { $device["VendorId"] } else { "" }
    $deviceId = if ($device) { $device["DeviceId"] } else { "" }

    $bars = @()
    if ($device) {
        foreach ($n in 0..5) {
            $decoded = ConvertFrom-Bar $device["Bar$n"]
            if ($decoded) {
                $bars += "BAR${n}:$($decoded.Space)@$($decoded.Base)" +
                         $(if ($decoded.Prefetchable) { "(pf)" } else { "" })
            }
        }
    }

    $bus = Get-BusVerdict $ini
    $ram = Get-InstalledRam $ini
    $vbeProvider = Get-VbeProvider $ini

    <#
      The option ROM's PCI Data Structure, as an identification route that needs
      no bus at all.

      Found on the 486 VLB run of 2026-08-21: a Diamond Stealth 64 DRAM on VESA
      Local Bus carries a valid `PCIR` header reporting 5333:8811, because
      Diamond shipped one BIOS image for both the PCI and the VLB variant of the
      board. So a card with no PCI to scan can still publish its PCI identity,
      out of its own ROM, to a purely read-only probe. That is worth checking
      before concluding a non-PCI card is unidentifiable - and it corroborates
      or contradicts the register read independently of it.
    #>
    $pcir = $null
    if ((Get-IniValue $ini "VideoBios" "PcirStatus") -eq "ok") {
        $pcirVendor = Get-IniValue $ini "VideoBios" "PcirVendorId"
        $pcirDevice = Get-IniValue $ini "VideoBios" "PcirDeviceId"
        if ($pcirVendor -and $pcirVendor -ne "FFFF" -and $pcirVendor -ne "0000") {
            $pcir = "$pcirVendor`:$pcirDevice"
        }
    }
    $aperture = Get-ApertureVerdict $ini $ram
    $crtcDifferences = Compare-CrtcBanks $ini

    <#
      Naming the S3 chip, best source first.

      The unlocked Tier 2 read is authoritative when it exists. Failing that,
      the Tier 1 CRTC dump already holds CR2D/CR2E/CR30 - it reads the whole
      0-3Fh bank without unlocking anything - so a report whose tester declined
      Tier 2 is still identifiable, provided the capture was from real hardware
      and not from a Windows DOS box where the VDD answers instead of the chip.
    #>
    $s3 = $null
    if ($ini.Contains("Chipset.S3")) {
        $s3 = Get-S3ChipName -DeviceIdHigh $ini["Chipset.S3"]["DeviceIdHigh"] `
                             -DeviceIdLow $ini["Chipset.S3"]["DeviceIdLow"] `
                             -ChipId $ini["Chipset.S3"]["ChipId"] `
                             -Source "Tier 2 unlocked read"
    }
    if ($null -eq $s3 -and
        (Get-IniValue $ini "VGARegisters" "Trust") -eq "hardware") {
        $crtc = Get-Blob $ini "VGARegisters" "Crtc"
        if ($null -ne $crtc -and $crtc.Length -gt 0x30) {
            $s3 = Get-S3ChipName -DeviceIdHigh ("{0:X2}" -f $crtc[0x2D]) `
                                 -DeviceIdLow ("{0:X2}" -f $crtc[0x2E]) `
                                 -ChipId ("{0:X2}" -f $crtc[0x30]) `
                                 -Source "Tier 1 locked dump"
        }
    }

    $cr58 = $null
    $cr36 = $null
    if ($ini.Contains("Chipset.S3")) {
        $cr58 = ConvertFrom-S3Cr58 $ini["Chipset.S3"]["CR58"]
        $cr36 = ConvertFrom-S3Cr36 $ini["Chipset.S3"]["CR36"]
    }

    $romStrings = @()
    if ($ini.Contains("VideoBios")) {
        foreach ($key in $ini["VideoBios"].Keys) {
            if ($key -like "String.*") { $romStrings += $ini["VideoBios"][$key] }
        }
    }

    return [pscustomobject]@{
        File         = Split-Path -Leaf $LiteralPath
        FullPath     = $LiteralPath
        Ini          = $ini
        Build        = Get-IniValue $ini "Report" "Build"
        Date         = Get-IniValue $ini "Report" "Date"
        Note         = Get-IniValue $ini "Report" "Note"
        Windows      = Get-IniValue $ini "System" "WindowsPresent"
        VendorId     = $vendorId
        DeviceId     = $deviceId
        SubsystemId  = if ($device) {
            "$($device['SubsystemVendorId']):$($device['SubsystemId'])"
        } else { "" }
        Revision     = if ($device) { $device["Revision"] } else { "" }
        ClassCode    = if ($device) { $device["ClassCode"] } else { "" }
        Bars         = ($bars -join " ")
        DisplayCount = Get-IniValue $ini "Result" "DisplayDeviceCount" "0"
        VbeVersion   = Get-IniValue $ini "VBE" "Version"
        VideoMemory  = Get-IniValue $ini "VBE" "TotalMemoryBytes"
        OemString    = Get-IniValue $ini "VBE" "OemString"
        ModeCount    = Get-IniValue $ini "VBEModes" "Count" "0"
        RomStrings   = $romStrings
        Chipset      = @($ini.Keys | Where-Object { $_ -like "Chipset*" }) -join ","
        Schema       = $schema
        Bus          = $bus
        RomPciId     = $pcir
        VbeProvider  = $vbeProvider
        IdentifiedBy = Get-IniValue $ini "Result" "IdentifiedBy" ""
        S3Chip       = $s3
        Cr58         = $cr58
        Cr36         = $cr36
        Ram          = $ram
        Aperture     = $aperture
        CrtcDiffs    = $crtcDifferences
        CpuClass     = Get-IniValue $ini "Platform" "CpuClass" ""
        CpuIdVendor  = Get-IniValue $ini "Platform" "CpuIdVendor" ""
        Monitor      = if ($edid -and $edid.Valid) {
            "$($edid.Manufacturer) $($edid.MonitorName)".Trim()
        } else { "" }
        Edid         = $edid
        Problems     = $problems
    }
}

# ---------------------------------------------------------------------------

$targets = @()
if (Test-Path -LiteralPath $Path -PathType Container) {
    $targets = @(Get-ChildItem -LiteralPath $Path -Filter "*.ini" -Recurse -File |
        ForEach-Object { $_.FullName })
} else {
    $targets = @((Resolve-Path -LiteralPath $Path).Path)
}
if ($targets.Count -eq 0) { throw "No report files found under $Path." }

$reports = @()
foreach ($target in $targets) {
    try {
        $reports += Read-SurveyReport -LiteralPath $target
    } catch {
        Write-Warning "Skipped $target : $($_.Exception.Message)"
    }
}
if ($reports.Count -eq 0) { throw "No readable survey reports." }

if ($ExtractRom) {
    if ($reports.Count -ne 1) { throw "-ExtractRom needs exactly one report." }
    $rom = Get-Blob $reports[0].Ini "VideoBios" "Rom"
    if ($null -eq $rom) { throw "That report carries no ROM image." }
    [IO.File]::WriteAllBytes($ExtractRom, $rom)
    $scope = Get-IniValue $reports[0].Ini "VideoBios" "DumpScope"
    Write-Output ("Wrote {0:N0} bytes to {1} (scope: {2})" -f $rom.Length, $ExtractRom, $scope)
    if ($scope -ne "full-image") {
        Write-Warning "This is a partial image. Ask the tester to re-run with /rom."
    }
}

if ($ExtractEdid) {
    if ($reports.Count -ne 1) { throw "-ExtractEdid needs exactly one report." }
    $block = Get-Blob $reports[0].Ini "EDID" "Block0"
    if ($null -eq $block) { throw "That report carries no EDID block." }
    [IO.File]::WriteAllBytes($ExtractEdid, $block)
    Write-Output ("Wrote {0:N0} bytes to {1}" -f $block.Length, $ExtractEdid)
}

foreach ($report in $reports) {
    Write-Output ""
    Write-Output ("=== {0} (schema {1}) ===" -f $report.File, $report.Schema)
    Write-Output ("  Bus          {0}" -f $report.Bus)
    if ($report.VendorId) {
        Write-Output ("  Card         {0}:{1} rev {2}  class {3}" -f
            $report.VendorId, $report.DeviceId, $report.Revision, $report.ClassCode)
        Write-Output ("  Subsystem    {0}" -f $report.SubsystemId)
        Write-Output ("  BARs         {0}" -f $report.Bars)
    }
    if ($report.RomPciId) {
        $note = ""
        if (-not $report.VendorId) {
            $note = "  [from the ROM's own PCIR header, no bus scan involved]"
        }
        Write-Output ("  ROM PCI id   {0}{1}" -f $report.RomPciId, $note)
    }
    if ($report.S3Chip) {
        $confidence = ""
        if (-not $report.S3Chip.Confident) {
            $confidence = "  [unverified - name comes from documentation, not measurement]"
        }
        Write-Output ("  Chipset      S3 {0} ({1} = {2}, {3}){4}" -f
            $report.S3Chip.Name, $report.S3Chip.From, $report.S3Chip.Key,
            $report.S3Chip.Source, $confidence)
    }
    Write-Output ("  VBE          v{0}, {1:N0} bytes VRAM, {2} modes" -f
        $report.VbeVersion, [int64]$report.VideoMemory, $report.ModeCount)
    if ($report.VbeProvider) {
        Write-Output ("  VBE from     {0} (mode list at segment {1})" -f
            $report.VbeProvider.Owner, $report.VbeProvider.Segment)
        if (-not $report.VbeProvider.IsRom) {
            Write-Output ("               so everything the VBE section claims - " +
                          "including any linear framebuffer - is that software's")
            Write-Output ("               promise, not a property of the card")
        }
    }
    Write-Output ("  OEM string   {0}" -f $report.OemString)
    if ($report.Monitor) { Write-Output ("  Monitor      {0}" -f $report.Monitor) }
    if ($report.Note)    { Write-Output ("  Tester note  {0}" -f $report.Note) }
    if ($report.Cr36) {
        $size = "code $($report.Cr36.Code), not decoded"
        if ($null -ne $report.Cr36.Bytes) {
            $size = "{0:N0} bytes" -f $report.Cr36.Bytes
        }
        Write-Output ("  VRAM (CR36)  {0} = {1}" -f $report.Cr36.Raw, $size)
    }
    if ($report.Cr58) {
        $enabled = "linear addressing OFF"
        if ($report.Cr58.LinearEnabled) { $enabled = "linear addressing ON" }
        Write-Output ("  Window       CR58={0} -> {1:N0} byte window, {2} (upper bits {3})" -f
            $report.Cr58.Raw, $report.Cr58.WindowSize, $enabled, $report.Cr58.UpperBits)
    }
    if ($report.Ram.Bytes) {
        Write-Output ("  RAM          {0:N0} bytes, per {1}" -f
            $report.Ram.Bytes, $report.Ram.Source)
    }
    if ($report.CpuClass) {
        $cpu = $report.CpuClass
        if ($report.CpuIdVendor) { $cpu = "$cpu, CPUID $($report.CpuIdVendor)" }
        Write-Output ("  CPU          {0}" -f $cpu)
    }
    if ($report.Aperture.Verdict -ne "absent") {
        Write-Output ("  Aperture     {0} - {1}" -f
            $report.Aperture.Verdict, $report.Aperture.Detail)
        foreach ($caveat in $report.Aperture.Caveats) {
            Write-Output ("               caveat: {0}" -f $caveat)
        }
    }
    if ($report.CrtcDiffs.Count -gt 0) {
        Write-Output ("  CR30-CR3F    locked and unlocked reads differ: {0}" -f
            ($report.CrtcDiffs -join ", "))
        Write-Output ("               on this part the locks gate reads too, so a")
        Write-Output ("               locked-read identification cannot be trusted")
    }
    if ($report.RomStrings.Count -gt 0) {
        Write-Output "  ROM strings:"
        foreach ($s in ($report.RomStrings | Select-Object -First 6)) {
            Write-Output "      $s"
        }
    }
    foreach ($problem in $report.Problems) { Write-Warning "  $problem" }

    if ($Detailed -and $report.Edid -and $report.Edid.Valid) {
        Write-Output "  EDID:"
        $report.Edid | Format-List | Out-String | Write-Output
    }
}

if ($Csv) {
    $rows = $reports | Select-Object File, Date, Build, Schema, Bus, IdentifiedBy,
        VendorId, DeviceId, Revision, SubsystemId, ClassCode, VideoMemory,
        VbeVersion, ModeCount, OemString, Monitor, Windows, Note, CpuClass,
        @{ n = "S3Chip";     e = { $_.S3Chip.Name } },
        @{ n = "S3ChipFrom"; e = { $_.S3Chip.From } },
        @{ n = "Cr36Bytes";  e = { $_.Cr36.Bytes } },
        @{ n = "Cr58";       e = { $_.Cr58.Raw } },
        @{ n = "WindowSize"; e = { $_.Cr58.WindowSize } },
        @{ n = "LinearOn";   e = { $_.Cr58.LinearEnabled } },
        @{ n = "RamBytes";   e = { $_.Ram.Bytes } },
        @{ n = "Aperture";   e = { $_.Aperture.Verdict } },
        @{ n = "VbeFromRom"; e = { $_.VbeProvider.IsRom } },
        @{ n = "Problems"; e = { $_.Problems -join "; " } }
    if (Test-Path -LiteralPath $Csv) {
        $rows | Export-Csv -LiteralPath $Csv -NoTypeInformation -Append
    } else {
        $rows | Export-Csv -LiteralPath $Csv -NoTypeInformation
    }
    Write-Output ""
    Write-Output "Appended $(@($rows).Count) row(s) to $Csv"
}

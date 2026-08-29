# Reports the PCI vendor/device id a video option ROM claims for itself.
#
# A PCI option ROM carries a PCI Data Structure - signature "PCIR" - whose
# vendor and device fields are what the system BIOS matches against the card
# before shadowing the image. So the ROM is a first-party statement of the id
# the silicon it shipped on publishes to configuration space, and reading a
# tree of ROM dumps answers "which part is device id NNNN" without a machine,
# a guest, or a card.
#
# It is evidence about hardware, not about an emulator: a ROM says what the
# board it came from claimed, while an emulator sets its own id in its own
# source and merely loads the image. Confirming an emulated target still needs
# a boot.
#
# Read-only. Point it at 86Box's roms\video tree or any directory of dumps:
#
#   .\scripts\read-option-rom-ids.ps1 -Path 'C:\86Box\roms\video\s3'
#
# Used to produce docs\decisions\2026-08-29-s3-device-id-survey.md.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string[]]$Path,
    # Report only ROMs whose PCIR was found, which is the usual case: pre-PCI
    # ISA and VLB dumps have no such structure and say nothing about an id.
    [switch]$MatchedOnly
)

$ErrorActionPreference = 'Stop'

# Offset 18h of an option ROM header holds the offset of the PCI Data
# Structure, itself laid out vendor at +04h, device at +06h and the three
# class-code bytes at +0Dh.
$RomPcirPointerOffset = 0x18
$PcirVendorOffset = 0x04
$PcirDeviceOffset = 0x06
$PcirClassOffset = 0x0D

foreach ($directory in $Path) {
    if (-not (Test-Path -LiteralPath $directory)) {
        throw "Required path does not exist: $directory"
    }

    Get-ChildItem -LiteralPath $directory -File | ForEach-Object {
        $file = $_
        $bytes = [IO.File]::ReadAllBytes($file.FullName)
        $row = [ordered]@{
            File = $file.Name
            Pcir = ''
            Vendor = ''
            Device = ''
            Class = ''
        }

        if ($bytes.Length -lt 0x20 -or $bytes[0] -ne 0x55 -or $bytes[1] -ne 0xAA) {
            $row.Pcir = 'no-rom-signature'
        } else {
            $pointer = [BitConverter]::ToUInt16($bytes, $RomPcirPointerOffset)
            if ($pointer -eq 0 -or ($pointer + $PcirClassOffset + 2) -ge $bytes.Length) {
                $row.Pcir = 'no-pcir-pointer'
            } elseif ([Text.Encoding]::ASCII.GetString($bytes, $pointer, 4) -ne 'PCIR') {
                $row.Pcir = '{0:X4}:not-pcir' -f $pointer
            } else {
                $row.Pcir = '{0:X4}' -f $pointer
                $row.Vendor = '{0:X4}' -f
                    [BitConverter]::ToUInt16($bytes, $pointer + $PcirVendorOffset)
                $row.Device = '{0:X4}' -f
                    [BitConverter]::ToUInt16($bytes, $pointer + $PcirDeviceOffset)
                $row.Class = '{0:X2}{1:X2}{2:X2}' -f
                    $bytes[$pointer + $PcirClassOffset + 2],
                    $bytes[$pointer + $PcirClassOffset + 1],
                    $bytes[$pointer + $PcirClassOffset]
            }
        }

        if ($MatchedOnly -and -not $row.Vendor) {
            return
        }
        [pscustomobject]$row
    }
}

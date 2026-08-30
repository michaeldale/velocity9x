# Velocity9x 0.6.5

Built from commit `b44c35f`: the output of
`scripts\build-all-packages.ps1` and `scripts\build-vga-survey.ps1`,
zipped for download.

Windows 98SE. Each zip carries its own instructions and its own recovery
notes - start with the `.TXT` files at the top level of the archive, and
read them before you install anything. This is a from-scratch display
driver: a bad install leaves the machine at a black screen until you
recover it.

## Which zip

| Download | Card | Hardware ID | Tested |
| --- | --- | --- | --- |
| [`velocity9x-0.6.5-ati.zip`](velocity9x-0.6.5-ati.zip) | ATI Mach64 / Rage | `PCI\VEN_1002&DEV_4C4D<br>PCI\VEN_1002&DEV_5654` | HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED |
| [`velocity9x-0.6.5-matrox-m2.zip`](velocity9x-0.6.5-matrox-m2.zip) | Matrox Millennium II | `PCI\VEN_102B&DEV_051B` | HOST-AUDITED; PHYSICAL ACTIVATION NOT YET TESTED |
| [`velocity9x-0.6.5-s3.zip`](velocity9x-0.6.5-s3.zip) | S3 | `PCI\VEN_5333&DEV_8810<br>PCI\VEN_5333&DEV_8811<br>PCI\VEN_5333&DEV_8812<br>PCI\VEN_5333&DEV_8813<br>PCI\VEN_5333&DEV_8814<br>PCI\VEN_5333&DEV_8901<br>PCI\VEN_5333&DEV_8A01` | HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED |
| [`velocity9x-0.6.5-vbe.zip`](velocity9x-0.6.5-vbe.zip) | VBE tier-0 (generic VESA) | `PCI\VEN_1234&DEV_1111` | HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED |

If your card is not listed, none of these will drive it. Run the survey
below and send the report in; that is what a new family is built from.

- **ATI Mach64 / Rage** modes: 640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz
- **S3** modes: 640x480, 800x600 and 1024x768 at 8/16/32 bpp, 1280x1024 at 8/16 bpp, 640x400 at 8 bpp, 60 Hz
- **VBE tier-0 (generic VESA)** modes: 640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz

## Hardware survey

[`velocity9x-survey-0.6.5.zip`](velocity9x-survey-0.6.5.zip) - a real-mode DOS program that reads
the PCI identifiers, video BIOS, advertised modes, monitor EDID and VGA
registers of whatever card is in the machine, and writes one report file.
It sets no video mode, installs nothing, and writes to no register.
Run it on an unsupported card and send the report; `README.TXT` inside
the zip has the instructions.

## Checksums

`SHA256SUMS.txt` covers the zips in this folder. Each package also ships
its own `SHA256.TXT` covering the files inside it.


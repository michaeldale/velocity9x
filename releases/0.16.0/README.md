# Velocity9x 0.16.0

Built from commit `bd63c52`: the output of
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
| [`velocity9x-0.16.0-ati.zip`](velocity9x-0.16.0-ati.zip) | ATI Mach64 / Rage | `PCI\VEN_1002&DEV_4742<br>PCI\VEN_1002&DEV_4744<br>PCI\VEN_1002&DEV_4747<br>PCI\VEN_1002&DEV_4749<br>PCI\VEN_1002&DEV_474C<br>PCI\VEN_1002&DEV_474D<br>PCI\VEN_1002&DEV_474E<br>PCI\VEN_1002&DEV_474F<br>PCI\VEN_1002&DEV_4750<br>PCI\VEN_1002&DEV_4751<br>PCI\VEN_1002&DEV_4752<br>PCI\VEN_1002&DEV_4753<br>PCI\VEN_1002&DEV_4754<br>PCI\VEN_1002&DEV_4755<br>PCI\VEN_1002&DEV_4756<br>PCI\VEN_1002&DEV_4757<br>PCI\VEN_1002&DEV_4759<br>PCI\VEN_1002&DEV_475A<br>PCI\VEN_1002&DEV_4C42<br>PCI\VEN_1002&DEV_4C44<br>PCI\VEN_1002&DEV_4C47<br>PCI\VEN_1002&DEV_4C49<br>PCI\VEN_1002&DEV_4C4D<br>PCI\VEN_1002&DEV_4C4E<br>PCI\VEN_1002&DEV_4C50<br>PCI\VEN_1002&DEV_4C51<br>PCI\VEN_1002&DEV_4C52<br>PCI\VEN_1002&DEV_4C53<br>PCI\VEN_1002&DEV_4C54<br>PCI\VEN_1002&DEV_5654<br>PCI\VEN_1002&DEV_5655<br>PCI\VEN_1002&DEV_5656` | HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED |
| [`velocity9x-0.16.0-intel-gma.zip`](velocity9x-0.16.0-intel-gma.zip) | Intel GMA (Gen3) | `PCI\VEN_8086&DEV_2592<br>PCI\VEN_8086&DEV_27AE` | HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED |
| [`velocity9x-0.16.0-matrox.zip`](velocity9x-0.16.0-matrox.zip) | Matrox Millennium | `PCI\VEN_102B&DEV_0519<br>PCI\VEN_102B&DEV_051B` | HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED |
| [`velocity9x-0.16.0-s3.zip`](velocity9x-0.16.0-s3.zip) | S3 | `PCI\VEN_5333&DEV_8810<br>PCI\VEN_5333&DEV_8811<br>PCI\VEN_5333&DEV_8812<br>PCI\VEN_5333&DEV_8813<br>PCI\VEN_5333&DEV_8814<br>PCI\VEN_5333&DEV_8901<br>PCI\VEN_5333&DEV_8904<br>PCI\VEN_5333&DEV_8A01<br>PCI\VEN_5333&DEV_8A13` | HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED |
| [`velocity9x-0.16.0-sis.zip`](velocity9x-0.16.0-sis.zip) | SiS 6326 | `PCI\VEN_1039&DEV_6326` | HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED |
| [`velocity9x-0.16.0-vbe.zip`](velocity9x-0.16.0-vbe.zip) | VBE tier-0 (generic VESA) | `PCI\VEN_1234&DEV_1111` | HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED |

If your card is not listed, none of these will drive it. Run the survey
below and send the report in; that is what a new family is built from.

- **ATI Mach64 / Rage** modes: 640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz
- **Intel GMA (Gen3)** modes: 640x480 and native 1024x576 at 8/16 bpp and 60 Hz
- **Matrox Millennium** modes: 640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz
- **S3** modes: 640x480, 800x600 and 1024x768 at 8/16/32 bpp, 1280x1024 at 8/16 bpp, 640x400 at 8 bpp, 60 Hz
- **SiS 6326** modes: 640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz
- **VBE tier-0 (generic VESA)** modes: 640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz

## Hardware survey

[`velocity9x-survey-0.16.0.zip`](velocity9x-survey-0.16.0.zip) - a real-mode DOS program that reads
the PCI identifiers, video BIOS, advertised modes, monitor EDID and VGA
registers of whatever card is in the machine, and writes one report file.
It sets no video mode, installs nothing, and writes to no register.
Run it on an unsupported card and send the report; `README.TXT` inside
the zip has the instructions.

## Checksums

`SHA256SUMS.txt` covers the zips in this folder. Each package also ships
its own `SHA256.TXT` covering the files inside it. `SIGNED.TXT`
lists the same zip hashes per family under an Ed25519 signature; it is
what the built-in updater (`V9XUPD.EXE`) checks before installing.


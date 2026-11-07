# The Trio64V+ gets its own Have Disk row, by PCI revision, on the Trio64 install

Date: 2026-11-06
Branch: `cursor/trio64vplus-86c765-plan-d326`

An 86Box guest boot with the released 0.37 driver is in this record. The
new Have Disk row itself was not installed there. No card. The host gates
are what was run on this branch.

## What was claimed before

[2026-08-29-s3-device-id-survey.md](2026-08-29-s3-device-id-survey.md) measured
the option ROMs and found that the Trio64V+ publishes PCI `5333:8811`, the
same device id as the Trio64. That conclusion stands. The Have Disk name is
a later question: the 0.38.0 S3 package has one model line for 8811, "S3
Trio32/64 86C764", so a Trio64V+ installs under that name.

## Evidence

The Trio64V+ data book (DB018-A, July 1995) distinguishes the chip by the
PCI revision, not by a second device id. CR2F is that revision, and bit 6
set is the Trio64V+ (section 13.3, CR2F, CR30; the book's example is 4xh).
The Trio32/64 book's revision is 00h. This is the same statement as
trio64-vbios-tools `docs/chip-rules.md` VP-ID-1 and T64-ID-1.

Two measured revisions, both with bit 6 set:

| Source | Revision |
|---|---|
| 86Box `S3_TRIO64V`, PCI config byte 08h (`src/video/vid_s3.c`) | 40h |
| Diamond Stealth64 Video 2001, VGA survey 2026-10-17 ([ini](../probe/references/s3-trio64vplus-pci-vogons-vgasurv-2026-10-17-d746481.ini), `Revision=53`) | 53h |

86Box is an emulator. The survey is a card. Neither value is stored in a
ROM: the seven Trio64V+ images in the 2026-11-06 VBIOS set (six distinct;
the two 1.03-08N files are the same bytes) all have PCIR vendor 5333,
device 8811, code revision 0000. The sign-on strings name the BIOS
(1.03-08N, Diamond 1.03, Hercules D2g, Phoenix 1.01-05, Phoenix 1.02-02,
STB 1.8), not the stepping.

S3's own Windows 95 driver, 4.10.01.2108-2.41.07, file `S3TRIO.INF` dated
1997-12-17, `VERSION.TXT` "S3 765/775/785", is the list of steppings S3
shipped a name for. Every Trio64V+ line is
`PCI\VEN_5333&DEV_8811&REV_xx`, description "S3 Inc. Trio64V+", install
section `S3_3`. The revisions are 40, 42-4F and 52-5F. 41h, 50h and 51h
are absent. All three have bit 6 set, so the book's test does not
explain the gap, and the INF does not say why. This row follows S3's
list rather than widening it. (The PR as first submitted said 50h and
51h lack bit 6; they do not, and its host test failed on it. Corrected
on merge.) The Trio64V2 line in that file is only `PCI\VEN_5333&DEV_8901`,
which this driver already has.

That INF does not justify new desktop modes. The 765 section and the 775
section install the same `s3_2.drv`. The extra 765 rows are refresh
rates, 1152x864, 1600x1200 and one 640x480x24 mode. This driver takes
modes from the BIOS through VBE. DB018-A calls packed 24 unaccelerated,
and the S3 BIOSes measured here report the high-colour VBE numbers as
32 bpp ([s3_hw16.c](../../src/chipsets/s3/s3_hw16.c)).

Section 1.4 of the same book splits the pinout. PD24 low at reset is the
local-peripheral-bus pinout, which is the PCI card. PD24 high is the
Trio64-socket pinout. PCI is the case this row is for. Four megabytes of
EDO is legal on that pinout; CR36 code 0 is already decoded as 4 MB, and
the linear window programmed in CR58 is already 4 MB. Streams, the Scenic
Highway and host pass-through (MMFF00 bits 3-1 = 100b, section 11.4) are
video paths. This driver does not program them, and the Trio64V2 hook
does not either: it opens the aperture and clears SR1A.

## The choice

One Have Disk name, `Velocity9x S3 Trio64V+ 86C765`, on
`V9x.Install.trio64`, the Trio64 install section. The ids are S3's list,
not every value from 40h to 5Fh. Each revision gets its own model line
with its `REV_` id as the hardware id, as S3's INF does, so "Show all
hardware" lists the name once per stepping.

The PR as submitted put all 29 ids on one line, `REV_40` as the hardware
id and the rest as compatible ids, to show the name once. That line is
891 characters. SetupX has not been seen to accept a line that long, and
the family manifest's own note records that Win98 Have Disk does not
always consult compatible ids. A parse failure would break every S3
install for the sake of a name, so the merge splits it the way S3 did.
Neither form has been booted.

`C:\V9XDIAG\V9XHW.INI` records the chip from CR2F, not from the row that
was installed. Bit 6 set writes `Adapter=S3 Trio64V+ 86C765` and
`PciRevision` as two hex digits. Bit 6 clear keeps `S3 Trio32/64 86C764`.
A 60h-7Fh card, which also has bit 6 set, still installs on the bare 8811
row and is still named a Trio64V+ in that file. The devnode `DeviceDesc`
is left as the INF wrote it.

No second `V9X_HW16_DEVICE`, no second backend-registry row, no CR53
write, no SR1A write, no mode-table change, no clock change.

The 86C764X and the 86C765 are not register-identical. The 765 has a
larger display FIFO with a priority threshold (MM81EC); CR54 is counted
in different units on the two chips; DDC on a PCI 765 uses the chip's
serial port, while a 764X board uses external latches; and the 765's
1-cycle EDO strap is reserved on the 764X, so the BIOS's memory-clock
choice differs. Those are BIOS mode-set and POST concerns. This row
assumes a matching PCI VBIOS on the card, takes modes through VBE, and
does not program CR54, the display FIFO, DDC or the memory type. Wrong
BIOS on the wrong chip is out of scope.

## Checked

`scripts/check-tree.ps1` generates the S3 INF and accepts the revision
row. It then stops on a bare LF in `packaging\win98se\INSTALL.TXT`,
which this change does not touch. The host tests for the CR2F name and
for `v9x_release_pci_id` reducing `REV_40` and `REV_53` to the bare id
are in the tree. They were not compiled here: this machine has no
Open Watcom.

On merge (2026-11-07, main after `8ad6bcab`), the host tests compiled and
two of them failed: they expected 50h and 51h to be named the Trio32/64,
but both have bit 6 set, so the name function was right and the tests
wrong. With the tests corrected and the INF split per revision,
`scripts/run-checks.ps1` passes, family packages included, and the S3
INF carries 29 Trio64V+ lines beside the bare 8811 line.

On 86Box, a Trio64V+ device with the Cardex BIOS and Velocity9x 0.37
(the S3 package before this row) installs and runs when Have Disk picks
the existing Trio32/64 86C764 entry. The Cardex pick is required: the
default Trio64V+ BIOS choice there is not a Trio64V+ image
([86Box/86Box#8757](https://github.com/86Box/86Box/issues/8757)). That
boot is the Trio64 install path on a revision-40h guest, not this
branch's package and not the new row.

## Not checked

This branch's S3 package was not built for that guest, so the new Have
Disk name was not selected. Whether SetupX prefers `REV_40` over the
bare 8811 id, and what the picture looks like under that row, is still
open. So is a revision-53h card.

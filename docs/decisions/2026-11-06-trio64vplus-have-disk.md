# The Trio64V+ gets its own Have Disk row, by PCI revision, on the Trio64 install

Date: 2026-11-06
Branch: `cursor/trio64vplus-86c765-plan-d326`

No guest and no card in this record. The host gates are what was run. A
Windows boot of a Trio64V+ is still open.

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
are absent. 50h and 51h do not have bit 6 set, so they fail the book's
test. The Trio64V2 line in that file is only `PCI\VEN_5333&DEV_8901`,
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

One Have Disk row, `Velocity9x S3 Trio64V+ 86C765`, on
`V9x.Install.trio64`, the Trio64 install section. The ids are S3's list,
not every value from 40h to 5Fh. `REV_40` is the hardware id. The other
revisions on that line are compatible ids, so the menu shows one name
rather than one copy of the name per stepping. S3 used one line per
revision with the same description. If a guest shows SetupX ignoring the
compatible ids, the line should be split the way S3 split it. That has
not been booted.

`C:\V9XDIAG\V9XHW.INI` records the chip from CR2F, not from the row that
was installed. Bit 6 set writes `Adapter=S3 Trio64V+ 86C765` and
`PciRevision` as two hex digits. Bit 6 clear keeps `S3 Trio32/64 86C764`.
A 60h-7Fh card, which also has bit 6 set, still installs on the bare 8811
row and is still named a Trio64V+ in that file. The devnode `DeviceDesc`
is left as the INF wrote it.

No second `V9X_HW16_DEVICE`, no second backend-registry row, no CR53
write, no SR1A write, no mode-table change, no clock change.

## Checked

`scripts/check-tree.ps1` generates the S3 INF and accepts the revision
row. It then stops on a bare LF in `packaging\win98se\INSTALL.TXT`,
which this change does not touch. The host tests for the CR2F name and
for `v9x_release_pci_id` reducing `REV_40` and `REV_53` to the bare id
are in the tree. They were not compiled here: this machine has no
Open Watcom.

## Not checked

No Windows boot. The preferred Have Disk row on a revision-40h guest, and
on a revision-53h card, is unmeasured. So is the picture.

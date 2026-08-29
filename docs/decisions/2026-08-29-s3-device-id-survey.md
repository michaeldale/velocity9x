# The Trio64V+ and Trio32 already publish 8811, so the driver has been binding them all along

Date: 2026-08-29
Branch: `s3-device-id-aliases`

Instrument: [`scripts/read-option-rom-ids.ps1`](../../scripts/read-option-rom-ids.ps1),
run over `C:\86Box\roms\video\s3` and `C:\86Box\roms\video\s3virge` (86Box 6.0,
41 dumps). No guest, no card.

## Method, and what it is worth

A PCI option ROM carries a PCI Data Structure at the offset in its header's
`18h` field, signature `PCIR`, with vendor at `+04h` and device at `+06h`. The
system BIOS matches those against the card before shadowing the image, so the
ROM is a first-party statement of the id the board it shipped on publishes to
configuration space.

That is evidence about **silicon**, not about 86Box: an emulator sets its own
id in its own source and merely loads the image. Confirming an emulated target
still needs a boot, which is why the `s3_trio32_pci` run in
[`2026-08-29-s3-trio32-alias-guest.md`](2026-08-29-s3-trio32-alias-guest.md)
exists alongside this.

## Measured

Every S3 ROM in the tree with a readable PCIR, grouped by the id it claims:

| Device | ROMs |
|---|---|
| `5631` | `86c325.bin`, `s3virge.bin`, `miro Crystal 3D 1.02.bin` |
| **`8811`** | **`S3T64VP.VBI`** (S3 Trio64V+ reference), **`86c732p.bin`** (Trio32), `86c764x1.bin`, `s3_764.bin`, `64V1506.ROM`, `stealt64.bin`, `octekmirage64.VBI`, `DiamondStealthSE.VBI`, `S3_764VL_SPEAMirageP64VL_ver5_03.BIN` |
| `883D` | `diamondstealth3000.vbi`, `stb_velocity3d_110.BIN` |
| `8880` | `1-DSV3868.BIN`, `numbernine.BIN` |
| `88B0` | `ELSA_Winner_10000_PCI_BIOS_3.04.02.BIN`, `ELSA_Winner_XHR_1000VL.BIN` |
| `88C0` / `88C1` | `bahamas64.bin`, `Miro20SD.BIN`, `S3_864_DEC_PCXAG-AL_19941117.bin` / `86c864p.bin` |
| `88D0` | `964_107h.rom`, `mirocrystal.VBI`, `elsaw20004m.BIN`, and two more |
| `88F0` | `1-DSV3968P.BIN`, `no9motionfx771.BIN`, `vv_303.rom`, and three more |
| **`8901`** | **`86c775_2.bin`** (Trio64V2/DX) |
| `8A01` | `86c375_1.bin`, `86c375_4.bin`, `virgedxdiamond.vbi` |
| `8A10` | `86c357.bin`, `flagpoint.VBI`, `DS3D4K v1.03 Brightness bug fix.bin` |
| `8A13` | `TRIO3D2X_8mbsdr.VBI` |

Nine ISA/VLB dumps carry no PCIR and say nothing about an id; they are reported
as `no-pcir-pointer` and excluded above.

## What this establishes

**The Trio64V+ 86C765 and the Trio32 86C732 publish `5333:8811`** - the id the
`trio64` chip has claimed since the family merge. Both install on the shipping
S3 package today with no manifest or code change. They were never unsupported,
only untested, and the same is true of the seven Trio64-family board ROMs
above.

**The Trio64V2/DX 86C775 publishes `5333:8901`**, which nothing claimed.

## What this disputes

[`scripts/parse-vga-survey.ps1`](../../scripts/parse-vga-survey.ps1) named
`8811` as `Trio32 or Trio64 (86C732/86C764)` and put `86C765` at `8814`. The
`S3T64VP.VBI` measurement contradicts the second half directly: 86C765 is at
`8811`. `8814` is the 86C767 Trio64UV+, which no dump here covers. Both labels
are corrected; the survey tool is what names a chip in a field report, so a
wrong label there is a wrong bug report later.

The table's header comment claimed the whole `S3DeviceIds` map was "as solid as
the PCI ids themselves". That was true of the *mechanism* - CR2D/CR2E do return
the PCI device id - and false of the *names*, which were documentation. The
comment now separates the two.

## What this does not establish

`8810`, `8812`, `8813` and `8814` appear in **no dump in this tree**. Their
names come from the public PCI id list and nothing here has seen the silicon.
They are added as *aliases* rather than chips precisely so that distinction
survives into the manifest: an alias binds an id and inherits a sibling's
measured behaviour, and the family's mode matrix does not cover it. See
[`docs/specifications/family-manifest.md`](../specifications/family-manifest.md).

`8901` is measured as an id and unmeasured as a driver target: no guest has run
it. It is an alias for the same reason, and it is the one of the five that could
be promoted to a chip cheaply, since 86Box emulates `trio64v2dx_pci`.

## Not investigated

The Vision864/868/964/968 and 86C928 ids above are real and unclaimed. They are
deliberately not added: those are external-RAMDAC boards whose CR36 memory
encoding differs from the Trio line, and
[`src/chipsets/s3/virge/memory.c`](../../src/chipsets/s3/virge/memory.c)
already refuses the codes they use rather than guessing. Binding them would be
a claim, not an alias.

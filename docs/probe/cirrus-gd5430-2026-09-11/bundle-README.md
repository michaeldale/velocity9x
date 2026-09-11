# cirrus-logic-gd5430-alpine — card evidence

Collected by BringupKit. Everything here is measured on one machine,
on one boot, unless a line says otherwise. Where something is not
known this says so rather than guessing.

## The card

- PCI identity: `1013:00a0` rev 47, class `0x030000`
- Location: `0000:04:06.0`, upstream bridge(s) `0000:00:14.4`
- Firmware-designated boot VGA: yes
- Kernel driver bound at capture: none

## Provenance

- Host: `bringup-target`, Linux bringupkit 6.12.107+deb13-amd64 #1 SMP PREEMPT_DYNAMIC Debian 6.12.107-1 (2026-08-29) x86_64 GNU/Linux
- Boot ID: `2cce947a-3d63-4cfa-8e6e-0a99a1df4acb`
- Captured: 2026-09-11T08:07:41Z
- BringupKit commit: `6dcc310260a2e20e8f67ef4d1192858e5c6eb66c`
- Controller sha256: `66ba3bdc2b96750e7d996c1c10eec3f48c70d7dc0ecf050dc3719d871e324e8f`
- Agent verbs used: pci-info, register-sweep, rom-capture
- Section coverage: 16 captured, 7 unsupported

## Video BIOS

- `evidence/rom.bin`, 32768 bytes, `full-image`, sha256 `0020fc81418f82a3f34b4269fbb2d0366a6932e2a19fae788062ba5b64714609`
- Source: **shadow**  — this is the copy firmware shadowed at 0xC0000 for the POSTed adapter, which is a RAM image POST may have patched. It is what int10 executes. It is **not** the ROM as shipped.
- Identifying strings:
  - `v2IBM VGA Compatible`
  - `CL-GD5440 VGA BIOS Version 1.00`
  - `Copyright 1992-1995 Cirrus Logic, Inc. All Rights Reserved.`
  - `Copyright 1987-1990 Quadtel Corp. All Rights Reserved.`
  - `VRGD64XX`
  - `Cirrus Logic GD-544x VGA`
  - `ttPSQRVWU`
  - `IZJmJtJ~J`
- PCIR: vendor `1013` device `00a0`, image 32768 bytes, last image  — matches the card, so the shadow is this card's BIOS

## Standard VGA register file

Read with trust `hardware`. No write characterization was run, so the writable columns are empty.
Attribute controller entry index byte: `0x20`.

> **CR11 bit 7 is set: the CRTC is write-protected as the BIOS left it.** Writes to CR00–CR06 are silently discarded until it is cleared — no error, no effect. CR11 itself is writable, so a driver can clear it.

| Register | Value after POST | Writable bits | Read-only bits | Side effects |
| --- | --- | --- | --- | --- |
| CR00 | 0x5F | not characterized | not characterized | — |
| CR01 | 0x4F | not characterized | not characterized | — |
| CR02 | 0x50 | not characterized | not characterized | — |
| CR03 | 0x82 | not characterized | not characterized | — |
| CR04 | 0x55 | not characterized | not characterized | — |
| CR05 | 0x81 | not characterized | not characterized | — |
| CR06 | 0xBF | not characterized | not characterized | — |
| CR07 | 0x1F | not characterized | not characterized | — |
| CR08 | 0x00 | not characterized | not characterized | — |
| CR09 | 0x4F | not characterized | not characterized | — |
| CR0A | 0x0D | not characterized | not characterized | — |
| CR0B | 0x0E | not characterized | not characterized | — |
| CR0C | 0x00 | not characterized | not characterized | — |
| CR0D | 0x00 | not characterized | not characterized | — |
| CR0E | 0x04 | not characterized | not characterized | — |
| CR0F | 0x10 | not characterized | not characterized | — |
| CR10 | 0x9C | not characterized | not characterized | — |
| CR11 | 0x9E | not characterized | not characterized | — |
| CR12 | 0x8F | not characterized | not characterized | — |
| CR13 | 0x28 | not characterized | not characterized | — |
| CR14 | 0x1F | not characterized | not characterized | — |
| CR15 | 0x96 | not characterized | not characterized | — |
| CR16 | 0xB9 | not characterized | not characterized | — |
| CR17 | 0xA3 | not characterized | not characterized | — |
| CR18 | 0xFF | not characterized | not characterized | — |
| SR00 | 0x03 | not characterized | not characterized | — |
| SR01 | 0x00 | not characterized | not characterized | — |
| SR02 | 0x03 | not characterized | not characterized | — |
| SR03 | 0x00 | not characterized | not characterized | — |
| SR04 | 0x02 | not characterized | not characterized | — |
| GR00 | 0x00 | not characterized | not characterized | — |
| GR01 | 0x00 | not characterized | not characterized | — |
| GR02 | 0x00 | not characterized | not characterized | — |
| GR03 | 0x00 | not characterized | not characterized | — |
| GR04 | 0x00 | not characterized | not characterized | — |
| GR05 | 0x10 | not characterized | not characterized | — |
| GR06 | 0x0E | not characterized | not characterized | — |
| GR07 | 0x00 | not characterized | not characterized | — |
| GR08 | 0xFF | not characterized | not characterized | — |

## Indexed vendor space

- **CR**: index decodes modulo **64**, exactly — so CR40 is CR00. CR19–CR3F are 39 distinct registers above the standard range, 39 of them reading other than 0xFF: vendor space, captured as measured and unattributed because no reviewed descriptor covers this family.
- **SR**: index decodes modulo **32**, exactly — so SR20 is SR00. SR05–SR1F are 27 distinct registers above the standard range, 27 of them reading other than 0xFF: vendor space, captured as measured and unattributed because no reviewed descriptor covers this family.
- **GR**: index decodes modulo **64**, exactly — so GR40 is GR00. GR09–GR3F are 55 distinct registers above the standard range, 45 of them reading other than 0xFF: vendor space, captured as measured and unattributed because no reviewed descriptor covers this family.

Verified across all 256 indexes of each bank, not sampled.

### Cirrus extended sequencer registers

- `SR06` = 0x12: the extension registers are readable, by the manual's own description of this register's readback (0x12 unlocked, 0x0F locked). Nothing below is decoded unless this says 0x12, so a locked bank cannot be mistaken for one whose registers read zero.
- `SR0F` = 0x11, bits 4:3 = 0x10: **1048576 bytes (1024 KiB) of display memory**, from the Alpine manual's own CL-GD5430/'40 column.
- `SR0F` bit 7 (DRAM Bank Switch Control) = 0; the manual scopes that bit to the CL-GD5434/'36, so it does not change the size on this part.
- `SR07` = 0x00, bits 7:4 = 0x0: **the linear aperture is closed.** The Alpine manual, on this field for a PCI part: "If this field is set to `0000', the CL-GD543X/'4X will respond to access at Axxx:x and Bxxx:x as a standard VGA." A frame buffer BAR that returns all-ones and master-aborts is the expected consequence, not a fault.

> Read, not written. The state of the aperture is a question this answers by looking at a register the sweep had already captured, so no mode register was touched to find out.

> Sources: Cirrus Logic, Alpine VGA Family CL-GD543X/4X Technical Reference Manual, 4th edition, February 1995 — the manual for this part — sections 9.1 SR6, 9.2 SR7 and 9.6 SRF. Corroborated by X.Org xf86-video-cirrus `src/alp_driver.c`, the Alpine driver for this chip, and by Linux `cirrusfb`, which map SR0F to the same sizes independently.

## Video BIOS mode list (VBE)

Obtained by executing the card's own BIOS on an emulated CPU with I/O passed through to the card — trust `emulated-int10`, which is a different class from a register read and must not be pooled with one. The BIOS itself was not modified and the card was not re-initialised (`card_reinitialised: False`).

- VBE 1.2, OEM `Cirrus Logic GD-544x VGA`
- Total memory the BIOS reports: 1048576 bytes. This is the BIOS's figure, not a measurement of installed VRAM. The BAR advertises 16777216 bytes, 16 times this figure, so the BIOS is not simply echoing the aperture size back.
- int 10h handler at `c000:3294`
- Configuration-space reads served to the BIOS: 0; writes refused: 0; foreign devices refused: 0

| Mode | Resolution | bpp | Bytes/scanline | Linear FB | PhysBasePtr | Memory model |
| --- | --- | --- | --- | --- | --- | --- |
| 0x014 | 132x25 | 4 | 264 | no | 0x00000000 | 0 |
| 0x10a | 132x43 | 4 | 264 | no | 0x00000000 | 0 |
| 0x109 | 132x25 | 4 | 264 | no | 0x00000000 | 0 |
| 0x102 | 800x600 | 4 | 100 | no | 0x00000000 | 3 |
| 0x103 | 800x600 | 8 | 800 | no | 0x00000000 | 4 |
| 0x104 | 1024x768 | 4 | 128 | no | 0x00000000 | 3 |
| 0x100 | 640x400 | 8 | 640 | no | 0x00000000 | 4 |
| 0x101 | 640x480 | 8 | 640 | no | 0x00000000 | 4 |
| 0x105 | 1024x768 | 8 | 1024 | no | 0x00000000 | 4 |
| 0x111 | 640x480 | 16 | 1280 | no | 0x00000000 | 6 |
| 0x114 | 800x600 | 16 | 1600 | no | 0x00000000 | 6 |
| 0x110 | 640x480 | 15 | 1280 | no | 0x00000000 | 6 |
| 0x113 | 800x600 | 15 | 1600 | no | 0x00000000 | 6 |
| 0x102 | 800x600 | 4 | 100 | no | 0x00000000 | 3 |
| 0x106 | 1280x1024 | 4 | 256 | no | 0x00000000 | 3 |
| 0x112 | 640x480 | 24 | 2048 | no | 0x00000000 | 6 |

**Scan-line pitch is not width x bytes-per-pixel for every mode.** A driver computing it will be wrong on:
- 0x110 (640x480x15): reports 1280, width x bpp would be 640
- 0x113 (800x600x15): reports 1600, width x bpp would be 800
- 0x112 (640x480x24): reports 2048, width x bpp would be 1920

### Logical scan line length (4F06h)

**4F06h here: declined in the mode the card was in: the gets returned 0x014f and 0x014f, and VBE success is 0x004f. Whether it would answer in a graphics mode is a different question from whether it exists.**

> No mode-set probe is in this folder, so whether 4F06h answers once a graphics mode is set was not tested. Do not read the line above as settling it.

EDID over DDC (4F15h): **not returned by this BIOS**. The call was made and did not answer, so EDID is unavailable on this card by this route as well as by capture.

## Frame buffer memory

- Aperture advertised by BAR 0: **16777216 bytes** (16 MiB). This is what the chip decodes, not what is installed behind it.
- Widest access that round-trips through the aperture: **4 bytes**
- Alias period measured: **131072 bytes** (128 KiB)

**Installed memory: not measured.**

The Cirrus linear window was opened, but SR07 bit 0 reads 0, so the chip is not in extended 256-color mode. The manual makes that the condition for the aperture to map one-to-one onto display memory, and says address wrapping occurs otherwise, so the 131072-byte period measured here belongs to the current mapping and is not the installed memory.

> The alias period above is a real measurement and it is **not** the installed memory; the reason is given above. What repeats is the mapping the card is currently in, not the DRAM behind it.

> Note also that this disagrees with the figure the BIOS reports through VBE 4F00h above. Neither is being preferred here: the BIOS figure is a report, this is a measurement of something else, and they are recorded separately so the disagreement stays visible.

## What is not here

- `edid`: unsupported (ddc-outside-baseline-contract)
- `runtime.driver`: unsupported (no-velocity9x-runtime)
- `survey.bios_data`: unsupported (no-dos-survey)
- `survey.platform`: unsupported (no-dos-survey)

## Files

- `evidence/baseline.json` (57034 bytes)
- `evidence/capture.ndjson` (20085 bytes)
- `evidence/cirrus-aperture-open3.ndjson` (8374 bytes)
- `evidence/cirrus-vbe-3.ndjson` (15178 bytes)
- `evidence/report.md` (2815 bytes)
- `evidence/rom-capture.ndjson` (80313 bytes)
- `evidence/rom.bin` (32768 bytes)

## Operator notes

- Cirrus Logic GD 5430/40 [Alpine], 1013:00a0 rev 47. ROM: CL-GD5440 VGA BIOS Version 1.00, Copyright 1992-1995 Cirrus Logic.
- The linear aperture WAS opened for this measurement, by setting SR07 bits 7:4 (Memory Segment Select) to the lowest non-zero value and restoring the register afterwards, verified. With it closed the BAR returns all-ones and master-aborts; with it open 32-bit accesses round-trip. The Alpine manual is explicit that on a PCI part that field only enables, and the address stays the one the kernel put in the BAR.
- The measurement still does not give installed memory, and the same manual says why: the aperture maps one-to-one onto display memory only in extended 256-color chain-4 addressing, and wraps otherwise. This card is in 80x25 text mode (SR04 bit 3 Chain-4 clear, GR05 bit 6 Shift256 clear, GR06 bit 0 alphanumeric, SR07 bit 0 clear), so the 131072-byte period is the period of the text-mode mapping.
- Installed memory is 1 MiB. Two independent readings agree: SR0F bits 4:3 = 10, which the Alpine manual's own CL-GD5430/40 column calls a 32-bit bus and 1 Mbyte, and the card's own BIOS reporting 1 MiB over VBE 4F00h.
- To measure rather than read it, the card would have to be put into a 256-colour mode and the aperture probed while it is there. Not done: that needs a mode set to persist across two verbs, which nothing here currently does.

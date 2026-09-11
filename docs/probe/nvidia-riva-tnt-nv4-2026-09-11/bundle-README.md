# nvidia-riva-tnt-nv4 — card evidence

Collected by BringupKit. Everything here is measured on one machine,
on one boot, unless a line says otherwise. Where something is not
known this says so rather than guessing.

## The card

- PCI identity: `10de:0020` rev 04, class `0x030000`
- Location: `0000:04:06.0`, upstream bridge(s) `0000:00:14.4`
- Firmware-designated boot VGA: yes
- Kernel driver bound at capture: none

## Provenance

- Host: `bringup-target`, Linux bringupkit 6.12.107+deb13-amd64 #1 SMP PREEMPT_DYNAMIC Debian 6.12.107-1 (2026-08-29) x86_64 GNU/Linux
- Boot ID: `355018cc-c1e1-4658-951d-1a42017c9c6f`
- Captured: 2026-09-11T07:33:01Z
- BringupKit commit: `0c90c06577ad6b4e4727f9942116fcf5558c4175`
- Controller sha256: `47b5fc61552f34d8c0669843499a3f9ed84282557ee5a60ca50bfd26d2af6a28`
- Agent verbs used: pci-info, register-sweep
- Section coverage: 15 captured, 1 refused, 7 unsupported

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
| CR0E | 0x06 | not characterized | not characterized | — |
| CR0F | 0x90 | not characterized | not characterized | — |
| CR10 | 0x9C | not characterized | not characterized | — |
| CR11 | 0x8E | not characterized | not characterized | — |
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

- **CR**: no simple aliasing; 228 indexes above the standard range read other than 0xFF
- **SR**: index decodes modulo **8**, exactly — so SR08 is SR00. SR05–SR07 are 3 distinct registers above the standard range, 3 of them reading other than 0xFF: vendor space, captured as measured and unattributed because no reviewed descriptor covers this family.
- **GR**: index decodes modulo **16**, exactly — so GR10 is GR00. GR09–GR0F are 7 distinct registers above the standard range, 7 of them reading other than 0xFF: vendor space, captured as measured and unattributed because no reviewed descriptor covers this family.

Verified across all 256 indexes of each bank, not sampled.

## Frame buffer memory

- Aperture advertised by BAR 1: **16777216 bytes** (16 MiB). This is what the chip decodes, not what is installed behind it.
- Widest access that round-trips through the aperture: **4 bytes**
- Alias period measured: none observed

**Installed memory: at least 16777216 bytes (16 MiB)**

No wrap and no decode edge anywhere in the 16777216-byte aperture: every probed offset up to 8388608 holds distinct storage that round-trips 32-bit accesses. This is a lower bound the aperture happens to cap, not an observed period.

## What is not here

- `edid`: unsupported (ddc-outside-baseline-contract)
- `rom.image`: refused (rom-capture-refused)
- `runtime.driver`: unsupported (no-velocity9x-runtime)
- `survey.bios_data`: unsupported (no-dos-survey)
- `survey.platform`: unsupported (no-dos-survey)
- `vbe.controller`: unsupported (no-real-mode-vbe)
- `vbe.modes`: unsupported (no-real-mode-vbe)

## Files

- `evidence/baseline.json` (55109 bytes)
- `evidence/capture.ndjson` (20454 bytes)
- `evidence/report.md` (2339 bytes)
- `evidence/rom-capture.ndjson` (5654 bytes)
- `evidence/tnt-aperture.ndjson` (8769 bytes)

## Operator notes

- NVIDIA Riva TNT (NV4), 10de:0020 rev 04. The ROM identifies the board as a Diamond Viper V550, BIOS Version 1.93E, Copyright 1996-1998 NVidia Corp.
- The card is healthy. Firmware POSTs it as primary VGA, bridge 00:14.4 forwards 32 MiB + 16 MiB so both BARs are positively decoded, PMC_BOOT_0 reads 0x20044001 (what nouveau independently reported as NV04), and the frame buffer measures at least 16 MiB.
- nouveau refuses the card: bios ctor failed -22, probe failed -22, after 'bios: OOB 4 00003b53 00003b53' and 'unable to locate usable image'. 0x3b53 is exactly this image's PCIR pointer, so nouveau followed it out of a buffer too short to hold it. The kernel's own shadow read fails too, with EIO at 0xC0000: no 0x55AA is there. The fault is the legacy shadow copy, not the card and not the ROM.
- The ROM was therefore read through the chip's own PROM window at BAR0+0x300000, by rom-capture-prom, and it is intact: 0x55AA, PCIR at 0x3b53 naming 10de:0020, last-image indicator set. sha256 34073870d7eb1ea22450b479b4ce7c98598f615c5cdc535340583f73dd0dc52e, archived under roms/10de-0020/. The bundle's rom.image section still reads refused because the baseline ran the ordinary capture; the image itself is in the archive.
- Opening the PROM window means clearing configuration offset 0x50 bit 0, which also turns this card's video output off while clear. It was cleared and restored within the one run, verified through both configuration space and its MMIO mirror at 0x1850, and the card was left as found. Nothing was attached to its output.
- Untested suggestion for making nouveau attach: nouveau.config=NvBios=PROM forces the PROM source, which is the one that demonstrably works here.
- Re-exported 11 September 2026 against a corrected exporter. The earlier copy of this document stated that this card has no indexed vendor register space, which was wrong: that conclusion was drawn from an alias modulus that actually establishes the opposite. The measured registers were always in the bundle; only the sentence about them was wrong.

# nvidia-tnt2-model64-nv5 — card evidence

Collected by BringupKit. Everything here is measured on one machine,
on one boot, unless a line says otherwise. Where something is not
known this says so rather than guessing.

## The card

- PCI identity: `10de:002d` rev 15, class `0x030000`
- Location: `0000:04:06.0`, upstream bridge(s) `0000:00:14.4`
- Firmware-designated boot VGA: yes
- Kernel driver bound at capture: none

## Provenance

- Host: `bringup-target`, Linux bringupkit 6.12.107+deb13-amd64 #1 SMP PREEMPT_DYNAMIC Debian 6.12.107-1 (2026-08-29) x86_64 GNU/Linux
- Boot ID: `2f78d05d-f028-47f6-9a91-6b4c44e84904`
- Captured: 2026-09-11T07:01:35Z
- BringupKit commit: `52c140a0217e74cbfe0f228b1ac7363dd8981385`
- Controller sha256: `a72d66d84d93ef2f979dacb7e586121137d70c1e121cd34fac38667f235f3894`
- Agent verbs used: pci-info, register-sweep, rom-capture
- Section coverage: 16 captured, 7 unsupported

## Video BIOS

- `evidence/rom.bin`, 46080 bytes, `full-image`, sha256 `11fd3f198e315c7d016a9cc3c019635a86c2646f8bcde9e3effe9338c443d3a9`
- Source: **shadow**  — this is the copy firmware shadowed at 0xC0000 for the POSTed adapter, which is a RAM image POST may have patched. It is what int10 executes. It is **not** the ROM as shipped.
- Identifying strings:
  - `IBM VGA Compatible`
  - `NVIDIA TNT2 Model 64 VGA BIOS`
  - `Version 3.05.00.10B25`
  - `Copyright (C) 1996-2000 NVidia Corp.`
  - `qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq`
  - `Riva TNT`
  - `Chip Rev B1`
  - `fVfWfSfRfQ`
- PCIR: vendor `10de` device `002d`, image 46080 bytes, last image  — matches the card, so the shadow is this card's BIOS

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
| CR04 | 0x54 | not characterized | not characterized | — |
| CR05 | 0x80 | not characterized | not characterized | — |
| CR06 | 0xBF | not characterized | not characterized | — |
| CR07 | 0x1F | not characterized | not characterized | — |
| CR08 | 0x00 | not characterized | not characterized | — |
| CR09 | 0x4F | not characterized | not characterized | — |
| CR0A | 0x0D | not characterized | not characterized | — |
| CR0B | 0x0E | not characterized | not characterized | — |
| CR0C | 0x00 | not characterized | not characterized | — |
| CR0D | 0x00 | not characterized | not characterized | — |
| CR0E | 0x06 | not characterized | not characterized | — |
| CR0F | 0x40 | not characterized | not characterized | — |
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

- **CR**: index decodes modulo **128**, exactly — so CR80 is CR00. CR19–CR7F are 103 distinct registers above the standard range, 103 of them reading other than 0xFF: vendor space, captured as measured and unattributed because no reviewed descriptor covers this family.
- **SR**: index decodes modulo **8**, exactly — so SR08 is SR00. SR05–SR07 are 3 distinct registers above the standard range, 3 of them reading other than 0xFF: vendor space, captured as measured and unattributed because no reviewed descriptor covers this family.
- **GR**: index decodes modulo **16**, exactly — so GR10 is GR00. GR09–GR0F are 7 distinct registers above the standard range, 7 of them reading other than 0xFF: vendor space, captured as measured and unattributed because no reviewed descriptor covers this family.

Verified across all 256 indexes of each bank, not sampled.

## Frame buffer memory

- Aperture advertised by BAR 1: **33554432 bytes** (32 MiB). This is what the chip decodes, not what is installed behind it.
- Widest access that round-trips through the aperture: **4 bytes**
- Alias period measured: none observed

**Installed memory: at least 33554432 bytes (32 MiB)**

No wrap and no decode edge anywhere in the 33554432-byte aperture: every probed offset up to 16777216 holds distinct storage that round-trips 32-bit accesses. This is a lower bound the aperture happens to cap, not an observed period.

## What is not here

- `edid`: unsupported (ddc-outside-baseline-contract)
- `runtime.driver`: unsupported (no-velocity9x-runtime)
- `survey.bios_data`: unsupported (no-dos-survey)
- `survey.platform`: unsupported (no-dos-survey)
- `vbe.controller`: unsupported (no-real-mode-vbe)
- `vbe.modes`: unsupported (no-real-mode-vbe)

## Files

- `evidence/baseline.json` (57333 bytes)
- `evidence/capture.ndjson` (20359 bytes)
- `evidence/report.md` (2813 bytes)
- `evidence/rom-capture.ndjson` (110261 bytes)
- `evidence/rom.bin` (46080 bytes)
- `evidence/tnt2-aperture-3.ndjson` (8873 bytes)

## Operator notes

- Card reseated 11 September 2026. Before the reseat firmware skipped this card entirely: it was not POSTed, bridge 00:14.4 forwarded only 1 MiB windows, and BAR0 answered roughly half of reads, always either the correct value or a master abort. That intermittency was the whole fault. After reseating, firmware POSTs it as primary VGA, the bridge windows are 32 MiB + 32 MiB, and nouveau brings the card up unaided.
- Installed memory is a lower bound. The aperture is 32 MiB and shows no wrap and no decode edge, which bounds the memory below but cannot prove the top half is populated: the wrap that would fix the size exactly lies outside the aperture. nouveau's independent read of the memory controller reports 32 MiB SDRAM and agrees.
- The ROM is the legacy shadow at 0xC0000, legitimate here because this card is the one firmware POSTed, but a RAM image POST may have patched rather than the ROM as shipped.
- nouveau was unbound from 0000:04:06.0 for the duration of the capture. Rebind with: echo 0000:04:06.0 > /sys/bus/pci/drivers/nouveau/bind
- Re-exported 11 September 2026 against a corrected exporter. The earlier copy of this document stated that this card has no indexed vendor register space, which was wrong: that conclusion was drawn from an alias modulus that actually establishes the opposite. The measured registers were always in the bundle; only the sentence about them was wrong.

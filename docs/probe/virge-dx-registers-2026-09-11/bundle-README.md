# s3-virge-dx — card evidence

Collected by BringupKit. Everything here is measured on one machine,
on one boot, unless a line says otherwise. Where something is not
known this says so rather than guessing.

## The card

- PCI identity: `5333:8a01` rev 01, class `0x030000`
- Location: `0000:04:06.0`, upstream bridge(s) `0000:00:14.4`
- Firmware-designated boot VGA: yes
- Kernel driver bound at capture: none

## Provenance

- Host: `bringupkit`, Linux bringupkit 6.12.107+deb13-amd64 #1 SMP PREEMPT_DYNAMIC Debian 6.12.107-1 (2026-08-29) x86_64 GNU/Linux
- Boot ID: `b8ee9ffc-d9f9-4970-a6af-324bc6cb567c`
- Captured: 2026-09-11T05:27:31Z
- BringupKit commit: not recorded — captured from a source snapshot with no git checkout on the target, so the tool version is pinned by `source.controller_sha256` below instead
- Controller sha256: `2bc7ee4138b8c1d7ba3a1347249c53cd93085419a104fac372c1bd481e4133a7`
- Agent verbs used: pci-info, register-sweep, s3-extended-read, rom-capture
- Section coverage: 16 captured, 7 unsupported

## Video BIOS

- `evidence/rom.bin`, 32768 bytes, `full-image`, sha256 `ca9c488b3d8fd1a33731457634688bfd7f7adfeaf0750e97b6ae0ed9091a01c7`
- Source: **shadow**  — this is the copy firmware shadowed at 0xC0000 for the POSTed adapter, which is a RAM image POST may have patched. It is what int10 executes. It is **not** the ROM as shipped.
- Identifying strings:
  - `TOXOIBM VGA COMPATIBLE BIOS.`
  - `S3 86C375/86C385 Video BIOS. Version 1.01.03`
  - `APAC S3375 VGA S3 Virge DX 70M`
  - `U[USUWUi`
  - `^_fPfSfR&`
  - `S3 Incorporated. 86C375/86C385`
  - `Ns+Nt7NuCNvONw[NxgNysNz`
- PCIR: vendor `5333` device `8a01`, image 32768 bytes, last image  — matches the card, so the shadow is this card's BIOS

## Standard VGA register file

Read with trust `hardware`. Writable-bit columns come from `register-probe`, which flips one bit at a time and restores it before the next.
Attribute controller entry index byte: `0x20`.

> **CR11 bit 7 is set: the CRTC is write-protected as the BIOS left it.** Writes to CR00–CR06 are silently discarded until it is cleared — no error, no effect. CR11 itself is writable, so a driver can clear it.

| Register | Value after POST | Writable bits | Read-only bits | Side effects |
| --- | --- | --- | --- | --- |
| CR00 | 0x5F | 0x00 | 0, 1, 2, 3, 4, 5, 6, 7 | none |
| CR01 | 0x4F | 0x00 | 0, 1, 2, 3, 4, 5, 6, 7 | none |
| CR02 | 0x50 | 0x00 | 0, 1, 2, 3, 4, 5, 6, 7 | none |
| CR03 | 0x82 | 0x00 | 0, 1, 2, 3, 4, 5, 6, 7 | none |
| CR04 | 0x55 | 0x00 | 0, 1, 2, 3, 4, 5, 6, 7 | none |
| CR05 | 0x81 | 0x00 | 0, 1, 2, 3, 4, 5, 6, 7 | none |
| CR06 | 0xBF | 0x00 | 0, 1, 2, 3, 4, 5, 6, 7 | none |
| CR07 | 0x1F | 0x10 | 0, 1, 2, 3, 5, 6, 7 | none |
| CR08 | 0x00 | 0x7F | 7 | none |
| CR09 | 0x4F | 0xFF | — | none |
| CR0A | 0x0D | 0x3F | 6, 7 | none |
| CR0B | 0x0E | 0x7F | 7 | none |
| CR0C | 0x00 | 0xFF | — | none |
| CR0D | 0x00 | 0xFF | — | none |
| CR0E | 0x06 | 0xFF | — | none |
| CR0F | 0x90 | 0xFF | — | none |
| CR10 | 0x9C | 0xFF | — | none |
| CR11 | 0x8E | 0xFF | — | none |
| CR12 | 0x8F | 0xFF | — | none |
| CR13 | 0x28 | 0xFF | — | none |
| CR14 | 0x1F | 0x7F | 7 | none |
| CR15 | 0x96 | 0xFF | — | none |
| CR16 | 0xB9 | 0xFF | — | none |
| CR17 | 0xA3 | 0xEF | 4 | none |
| CR18 | 0xFF | 0xFF | — | none |
| SR00 | 0x03 | 0x03 | 2, 3, 4, 5, 6, 7 | none |
| SR01 | 0x00 | 0x3D | 1, 6, 7 | none |
| SR02 | 0x03 | 0x0F | 4, 5, 6, 7 | none |
| SR03 | 0x00 | 0x3F | 6, 7 | none |
| SR04 | 0x02 | 0x0E | 0, 4, 5, 6, 7 | none |
| GR00 | 0x00 | 0x0F | 4, 5, 6, 7 | none |
| GR01 | 0x00 | 0x0F | 4, 5, 6, 7 | none |
| GR02 | 0x00 | 0x0F | 4, 5, 6, 7 | none |
| GR03 | 0x00 | 0x1F | 5, 6, 7 | none |
| GR04 | 0x00 | 0x03 | 2, 3, 4, 5, 6, 7 | none |
| GR05 | 0x10 | 0x7B | 2, 7 | none |
| GR06 | 0x0E | 0x0F | 4, 5, 6, 7 | none |
| GR07 | 0x00 | 0x0F | 4, 5, 6, 7 | none |
| GR08 | 0xFF | 0xFF | — | none |

## Indexed vendor space

- **CR**: no simple aliasing; 223 indexes above the standard range read other than 0xFF
- **SR**: index decodes modulo **64**, exactly — so SR40 is SR00 and there is no vendor register space behind this port pair
- **GR**: index decodes modulo **16**, exactly — so GR10 is GR00 and there is no vendor register space behind this port pair

Verified across all 256 indexes of each bank, not sampled.

## Video BIOS mode list (VBE)

Obtained by executing the card's own BIOS on an emulated CPU with I/O passed through to the card — trust `emulated-int10`, which is a different class from a register read and must not be pooled with one. The BIOS itself was not modified and the card was not re-initialised (`card_reinitialised: False`).

- VBE 1.2, OEM `S3 Incorporated. 86C375/86C385`
- Total memory the BIOS reports: 4194304 bytes. This is the BIOS's figure, not a measurement of installed VRAM, and it matches the aperture size, so treat it as a report rather than an independent confirmation.
- int 10h handler at `c000:5300`
- Configuration-space reads served to the BIOS: 0; writes refused: 0; foreign devices refused: 0

| Mode | Resolution | bpp | Bytes/scanline | Linear FB | PhysBasePtr | Memory model |
| --- | --- | --- | --- | --- | --- | --- |
| 0x100 | not queried | | | | | |
| 0x101 | not queried | | | | | |
| 0x102 | not queried | | | | | |
| 0x103 | not queried | | | | | |
| 0x104 | not queried | | | | | |
| 0x105 | not queried | | | | | |
| 0x106 | not queried | | | | | |
| 0x107 | not queried | | | | | |
| 0x109 | not queried | | | | | |
| 0x10a | not queried | | | | | |
| 0x10d | not queried | | | | | |
| 0x10e | not queried | | | | | |
| 0x10f | not queried | | | | | |
| 0x110 | not queried | | | | | |
| 0x111 | not queried | | | | | |
| 0x112 | not queried | | | | | |
| 0x113 | not queried | | | | | |
| 0x114 | not queried | | | | | |
| 0x115 | not queried | | | | | |
| 0x116 | not queried | | | | | |
| 0x117 | not queried | | | | | |
| 0x118 | not queried | | | | | |
| 0x119 | not queried | | | | | |
| 0x11a | not queried | | | | | |
| 0x120 | not queried | | | | | |

### Logical scan line length (4F06h)

**4F06h here: not answered: BL=01h and BL=03h did not complete under emulation, so the call returned no status at all. That is a fact about running this handler here, NOT evidence that the BIOS lacks the function -- a mode set may be a precondition for it. See the mode-set section if there is one.**

> **The mode-set section below contradicts any reading of this as an absent function, and it is the stronger evidence.** With a graphics mode set, the same call moved the scan line length. Whatever happened above is about asking in the card's power-on text mode, not about what the BIOS can do.

EDID over DDC (4F15h): **not returned by this BIOS**. The call was made and did not answer, so EDID is unavailable on this card by this route as well as by capture.

## Frame buffer memory

- Aperture advertised by BAR 0: **67108864 bytes** (64 MiB). This is what the chip decodes, not what is installed behind it.
- Widest access that round-trips through the aperture: **4 bytes**
- Alias period measured: **4194304 bytes** (4096 KiB)

**Installed memory: 4194304 bytes (4 MiB)**

The card stops decoding at 4194304 bytes: everything below it is real, distinct storage and the first address at or past it returns all-ones. a decode edge is a sharper measurement than an alias period, because it says where the memory ends rather than what the mapping repeats at.

## Setting a mode, and the scan line length

The only measurement here taken with the card in a mode its firmware did not leave it in. The BIOS was asked to set a mode, questioned about the scan line length, and then asked to put both back — every step through its own code, with no register written directly.

- Mode on entry: `0x0003` (reported by 4F03h, not assumed)
- Mode requested: `0x0117`, 4F02h returned `0x004f`
- Mode afterwards: `0x0117`
- Mode on exit: `0x0003`, restored: **yes**

In that mode the BIOS reported a scan line length of **2048 bytes** (1024 pixels, 2048 scan lines). Unlike the figure in the section above, this describes a real graphics mode.

### Will this BIOS accept a scan line length it did not choose?

| Subfunction | Requested | Result | Length moved |
| --- | --- | --- | --- |
| `BL=02h` (bytes) | 2304 | status `0x014f`, 2048 bytes, 1024 pixels, 2048 lines | no |
| `BL=02h` (bytes) | 4096 | status `0x014f`, 2048 bytes, 1024 pixels, 2048 lines | no |
| `BL=00h` (pixels) | 1152 | status `0x004f`, 2304 bytes, 1152 pixels, 1820 lines | **yes** |

> **The length is settable, but only in pixels.** `4F06h BL=00h` changes it. `4F06h BL=02h`, which asks for the same thing in bytes, **returns success and leaves the length alone** — including when asked for a value it demonstrably can produce, since the pixel request above landed on exactly the byte count the byte request was refused for.

> A driver that sets the pitch through `BL=02h` on this BIOS will get a success code and an unchanged pitch. That is the failure most likely to present as a corrupted display and be mistaken for a driver bug. Use `BL=00h`.

> This took three attempts to establish. One request, in bytes, returned success without effect, and that is indistinguishable from a granularity the request did not meet. Reporting it as "not settable" would have been wrong, and was: the first run of this probe said exactly that.

Length after the attempts: 2048 bytes, restored: **yes**.

## What is not here

- `aperture.probe`: unsupported (no-aperture-probe-in-baseline)
- `edid`: unsupported (ddc-outside-baseline-contract)
- `runtime.driver`: unsupported (no-velocity9x-runtime)
- `survey.bios_data`: unsupported (no-dos-survey)
- `survey.platform`: unsupported (no-dos-survey)
- `vbe.controller`: unsupported (no-real-mode-vbe)
- `vbe.modes`: unsupported (no-real-mode-vbe)

## Files

- `evidence/aperture.ndjson` (8537 bytes)
- `evidence/baseline.json` (55529 bytes)
- `evidence/capture.ndjson` (22998 bytes)
- `evidence/modeprobe.ndjson` (10309 bytes)
- `evidence/probe-crtc.ndjson` (10510 bytes)
- `evidence/probe-gc.ndjson` (7428 bytes)
- `evidence/probe-seq.ndjson` (6742 bytes)
- `evidence/report.md` (3245 bytes)
- `evidence/rom-capture.ndjson` (79997 bytes)
- `evidence/rom.bin` (32768 bytes)
- `evidence/vbe.ndjson` (8118 bytes)

## Operator notes

- S3 ViRGE/DX in the FX-6300 bench (GA-78LMT-USB3). No display attached to the card; console is on the RTX 2060 at 0000:01:00.0.
- Firmware POSTs this card with bus mastering enabled; it was cleared by bus-master-clear before anything else ran, and returns at the next POST.
- The S3 vendor register descriptor is the Trio64 list applied to the ViRGE/DX by family resemblance, not from a datasheet for this part. See descriptors/s3-virge-dx-v1.json sources.

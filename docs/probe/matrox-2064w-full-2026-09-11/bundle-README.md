# matrox-mga-2064w — card evidence

Collected by BringupKit. Everything here is measured on one machine,
on one boot, unless a line says otherwise. Where something is not
known this says so rather than guessing.

## The card

- PCI identity: `102b:0519` rev 01, class `0x030000`
- Location: `0000:04:06.0`, upstream bridge(s) `0000:00:14.4`
- Firmware-designated boot VGA: yes
- Kernel driver bound at capture: none

## Provenance

- Host: `bringupkit`, Linux bringupkit 6.12.107+deb13-amd64 #1 SMP PREEMPT_DYNAMIC Debian 6.12.107-1 (2026-08-29) x86_64 GNU/Linux
- Boot ID: `195e37e1-bf59-4176-a743-e60810f581ca`
- Captured: 2026-09-11T03:42:36Z
- BringupKit commit: not recorded — captured from a source snapshot with no git checkout on the target, so the tool version is pinned by `source.controller_sha256` below instead
- Controller sha256: `68f624680416ce7f965de9c770eca1a0b1220e92eeb18f7b51346c6c7414b302`
- Agent verbs used: pci-info, register-sweep, rom-capture
- Section coverage: 16 captured, 7 unsupported

## Video BIOS

- `evidence/rom.bin`, 32768 bytes, `full-image`, sha256 `99903fd4f1f17f3a1dc4b62b6b47a79e4e5dee55750da2827ddfd898f0885ee8`
- Source: **shadow**  — this is the copy firmware shadowed at 0xC0000 for the POSTed adapter, which is a RAM image POST may have patched. It is what int10 executes. It is **not** the ROM as shipped.
- Identifying strings:
  - `IBM COMPATIBLE MATROX/MILLENNIUM  VGA/VBE BIOS (V1.9 )`
  - `MATROX POWER GRAPHICS ACCELERATOR`
  - `MGA Series`
  - `VGA/VBE BIOS, Version V1.9`
  - `Copyright (C) 1995, Matrox Graphics Inc.`
- PCIR: vendor `102b` device `0519`, image 32768 bytes, last image  — matches the card, so the shadow is this card's BIOS

## Standard VGA register file

Read with trust `hardware`. Writable-bit columns come from `register-probe`, which flips one bit at a time and restores it before the next.
Attribute controller entry index byte: `0x20`.

> **CR11 bit 7 is set: the CRTC is write-protected as the BIOS left it.** Writes to CR00–CR06 are silently discarded until it is cleared — no error, no effect. CR11 itself is writable, so a driver can clear it.

| Register | Value after POST | Writable bits | Read-only bits | Side effects |
| --- | --- | --- | --- | --- |
| CR00 | 0x60 | 0x00 | 0, 1, 2, 3, 4, 5, 6, 7 | none |
| CR01 | 0x4F | 0x00 | 0, 1, 2, 3, 4, 5, 6, 7 | none |
| CR02 | 0x50 | 0x00 | 0, 1, 2, 3, 4, 5, 6, 7 | none |
| CR03 | 0x83 | 0x00 | 0, 1, 2, 3, 4, 5, 6, 7 | none |
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
| CR0E | 0x00 | 0xFF | — | none |
| CR0F | 0x00 | 0xFF | — | none |
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

- **CR**: index decodes modulo **64**, exactly — so CR40 is CR00 and there is no vendor register space behind this port pair
- **SR**: index decodes modulo **8**, exactly — so SR08 is SR00 and there is no vendor register space behind this port pair
- **GR**: index decodes modulo **16**, exactly — so GR10 is GR00 and there is no vendor register space behind this port pair

Verified across all 256 indexes of each bank, not sampled.

## Video BIOS mode list (VBE)

Obtained by executing the card's own BIOS on an emulated CPU with I/O passed through to the card — trust `emulated-int10`, which is a different class from a register read and must not be pooled with one. The BIOS itself was not modified and the card was not re-initialised (`card_reinitialised: False`).

- VBE 2.0, OEM `Matrox Graphics Inc.`
- Total memory the BIOS reports: 8388608 bytes. This is the BIOS's figure, not a measurement of installed VRAM, and it matches the aperture size, so treat it as a report rather than an independent confirmation.
- int 10h handler at `c000:1930`
- Configuration-space reads served to the BIOS: 1528; writes refused: 3347; foreign devices refused: 6834

| Mode | Resolution | bpp | Bytes/scanline | Linear FB | PhysBasePtr | Memory model |
| --- | --- | --- | --- | --- | --- | --- |
| 0x100 | 640x400 | 8 | 640 | **yes** | 0xfd000000 | 4 |
| 0x101 | 640x480 | 8 | 640 | **yes** | 0xfd000000 | 4 |
| 0x102 | 800x600 | 4 | 100 | no | 0xfd000000 | 3 |
| 0x103 | 800x600 | 8 | 960 | **yes** | 0xfd000000 | 4 |
| 0x105 | 1024x768 | 8 | 1024 | **yes** | 0xfd000000 | 4 |
| 0x107 | 1280x1024 | 8 | 1280 | **yes** | 0xfd000000 | 4 |
| 0x108 | 80x60 | 4 | 80 | no | 0xfd000000 | 0 |
| 0x109 | 132x25 | 4 | 132 | no | 0xfd000000 | 0 |
| 0x10b | 132x50 | 4 | 132 | no | 0xfd000000 | 0 |
| 0x10c | 132x60 | 4 | 132 | no | 0xfd000000 | 0 |
| 0x110 | 640x480 | 16 | 1280 | **yes** | 0xfd000000 | 6 |
| 0x111 | 640x480 | 16 | 1280 | **yes** | 0xfd000000 | 6 |
| 0x112 | 640x480 | 32 | 2560 | **yes** | 0xfd000000 | 6 |
| 0x113 | 800x600 | 16 | 1920 | **yes** | 0xfd000000 | 6 |
| 0x114 | 800x600 | 16 | 1920 | **yes** | 0xfd000000 | 6 |
| 0x115 | 800x600 | 32 | 3200 | **yes** | 0xfd000000 | 6 |
| 0x116 | 1024x768 | 16 | 2048 | **yes** | 0xfd000000 | 6 |
| 0x117 | 1024x768 | 16 | 2048 | **yes** | 0xfd000000 | 6 |
| 0x11c | 1600x1200 | 8 | 1600 | **yes** | 0xfd000000 | 4 |
| 0x118 | 1024x768 | 32 | 4096 | **yes** | 0xfd000000 | 6 |
| 0x119 | 1280x1024 | 16 | 2560 | **yes** | 0xfd000000 | 6 |
| 0x11a | 1280x1024 | 16 | 2560 | **yes** | 0xfd000000 | 6 |
| 0x11d | 1600x1200 | 16 | 3200 | **yes** | 0xfd000000 | 6 |
| 0x11e | 1600x1200 | 16 | 3200 | **yes** | 0xfd000000 | 6 |

**Scan-line pitch is not width x bytes-per-pixel for every mode.** A driver computing it will be wrong on:
- 0x103 (800x600x8): reports 960, width x bpp would be 800
- 0x113 (800x600x16): reports 1920, width x bpp would be 1600
- 0x114 (800x600x16): reports 1920, width x bpp would be 1600

### Logical scan line length (4F06h)

**4F06h is implemented, with a caveat: BL=03h returned success but left BX, DX holding the value it went in with.** Only the two get subfunctions were issued (BL=01h and BL=03h); neither set subfunction was called, so nothing about the display was changed.

| Subfunction | Status | Bytes/scan line | Pixels/scan line | Scan lines |
| --- | --- | --- | --- | --- |
| BL=01h current | `0x004f` | 80 | 640 | 638 |
| BL=03h maximum | `0x004f` | *not written* | 800 | *not written* |

> The *numbers* here describe whatever mode the card is in, which during this capture was its power-on VGA text mode — so read them as evidence the call works, not as a description of a graphics mode. The status is the load-bearing part.

> Cells marked *not written* are registers the BIOS left holding the value they went in with. CX and DX were loaded with `0xa5a5` beforehand and BX carries the subfunction number, so a register returning that exact value was not answered rather than answered with a small number.

EDID over DDC (4F15h): **not returned by this BIOS**. The call was made and did not answer, so EDID is unavailable on this card by this route as well as by capture.

## Device-specific configuration space

Read-only. Decoded field by field against a cited document; every field below names the register it came from.

### OPTION at `0x40` = `0x5f2c0100`

| Field | Bits | Value | Access |
| --- | --- | --- | --- |
| `vgaioen` | 8 | 1 | rw |
| `interleave` | 12 | 0 | rw |
| `rfhcnt` | 19:16 | 12 | rw |
| `eepromwt` | 20 | 0 | rw |
| `nogscale` | 21 | 1 | rw |
| `productid` | 28:24 | 31 | ro |
| `noretry` | 29 | 0 | rw |
| `biosen` | 30 | 1 | rw |
| `powerpc` | 31 | 0 | rw |

Source: MGA-2064W Specification 10470-MS-0300, Feb 1996, OPTION (address 40h, CS), p.4-13

- **vgaioen** = 1: VGA I/O map enable. 1 = VGA I/O locations are decoded. On hard reset the sampled vgaboot strap replaces this value. The MGA control registers and frame buffer map are enabled regardless.
- **interleave** = 0: Memory interleave enable. Non-interleave must be selected with a 32-bit RAMDAC, or with a 64-bit RAMDAC when only the first 2 MBytes are displayable; 4 or 8 MBytes with a 64-bit RAMDAC require interleave. Must be 0 for VGA mode (mgamode = 0).
- **rfhcnt** = 12: Memory refresh counter. rfhcnt = (refresh period us * gclk MHz / gscaling_factor / 128) - 1, where gscaling_factor is 4 when nogscale = 0 and 1 when nogscale = 1.
- **eepromwt** = 0: EEPROM write enable. 1 = a write to the BIOS EPROM aperture programs that location; 0 = no effect.
- **nogscale** = 1: Graphic clock pre-scaler. 0 = gclk divided by 4 internally; 1 = gclk not divided.
- **productid** = 31: Product ID straps: sampled state of the MDQ<4:0> pins after hard reset. Board-defined, not chip-defined -- the specification offers that a board designer COULD encode memory size and RAMDAC type here, and states these bits do not control hardware within the chip. Do not read an installed memory size out of this field; measure it.
- **noretry** = 0: Retry disable. 1 = retries are disabled during the initial data phase of any transfer to the MGA-2064W, a workaround for a specific PCI chipset.
- **biosen** = 1: BIOS enable. 0 = the ROMBASE space is disabled; 1 = enabled, and rombase must be initialised because it otherwise contains unpredictable data.
- **powerpc** = 0: PowerPC mode. 1 = byte swapping is enabled for mgabase1 + 1C00h to mgabase1 + 1EFFh so a big endian processor sees the same layout as a little endian one.
### MGA_INDEX at `0x44` = `0x00003c0a`

| Field | Bits | Value | Access |
| --- | --- | --- | --- |
| `index` | 31:0 | 15370 | rw |

Source: MGA-2064W Specification 10470-MS-0300, Feb 1996, MGA_INDEX (address 44h, CS), p.4-6

- **index** = 15370: Control-register address that MGA_DATA will read or write. Reading this register reports the address currently latched; it performs no indirect access of its own.

## Frame buffer memory

- Aperture advertised by BAR 1: **8388608 bytes** (8 MiB). This is what the chip decodes, not what is installed behind it.
- Widest access that round-trips through the aperture: **4 bytes**
- Alias period measured: **8388608 bytes** (8192 KiB)

**Installed memory: 8388608 bytes (8 MiB)**

The aperture round-trips 32-bit accesses and repeats every 8388608 bytes.

> **Measured with the aperture mode bit set.** The card's frame buffer aperture is gated by CRTCEXT3 bit 7, and the firmware leaves that bit clear. The probe set it for the duration of the measurement and restored it afterwards (CRTCEXT3 `0x00` → `0x80` → `0x00`, restored: yes). The figure above therefore describes the memory behind the aperture, but the card as you will find it is back in VGA mode, where that aperture is not usable.

> Nothing else was written: no clock, no CRTC timing, no memory controller setting. The measurement itself is the same code that runs without the mode bit, so the difference between the two results is a difference in the card and not in how it was measured.

## RAMDAC and clocks

Read from an indexed register file at BAR 0 offset `0x3c00`.

**The mapping is proved, not assumed.** The MGA specification states that where the RAMDAC lands inside its aperture depends on the board, so before decoding anything the probe reads the part's own ID register (index `0x3f`) and requires `0x26`. It read `0x26` — confirmed.

| Register | Index | Value |
| --- | --- | --- |
| Silicon Revision | `0x01` | `0x11` |
| Indirect Cursor Control | `0x06` | `0x00` |
| Latch Control | `0x0f` | `0x06` |
| True Color Control | `0x18` | `0x80` |
| Multiplex Control | `0x19` | `0x98` |
| Clock Selection | `0x1a` | `0x00` |
| Palette Page | `0x1c` | `0x00` |
| General Control | `0x1d` | `0x00` |
| Miscellaneous Control | `0x1e` | `0x00` |
| MCLK/Loop Clock Control | `0x39` | `0x18` |
| ID | `0x3f` | `0x26` |

### Clocks

Synthesised from N, M and P by `f_vco = 8 * f_ref * (65 - m) / (65 - n); f_pll = f_vco / 2**p` with a 14.31818 MHz reference.

| PLL | N | M | P | Frequency | Locked | Drives dot clock |
| --- | --- | --- | --- | --- | --- | --- |
| pixel | 40 | 17 | 3 | **27.491 MHz** | yes | no |
| mclk | 61 | 58 | 2 | **50.114 MHz** | yes | — |
| loop | 57 | 61 | 2 | **14.318 MHz** | yes | — |

The dot clock source register (`0x1a` = `0x00`) selects **CLK0, for use with LCLK latching of the VGA port**.

> **The pixel clock PLL is programmed and locked but is not driving the display.** A PLL can hold a frequency while something else feeds the dot clock, and that is the state the firmware leaves this card in. The pixel figure above is what the PLL is set to, not the rate the display is running at.

Reference clock: TVP3026 datasheet section 2.4.1: Appendix A gives register values for the common 14.31818 MHz reference, and the fixed PLLSEL frequencies assume a standard 14.31818 MHz crystal. This is an assumption about the board, not a measurement of it.

## Setting a mode, and the scan line length

The only measurement here taken with the card in a mode its firmware did not leave it in. The BIOS was asked to set a mode, questioned about the scan line length, and then asked to put both back — every step through its own code, with no register written directly.

- Mode on entry: `0x0003` (reported by 4F03h, not assumed)
- Mode requested: `0x0117`, 4F02h returned `0x004f`
- Mode afterwards: `0x0117`
- Mode on exit: `0x0003`, restored: **yes**

In that mode the BIOS reported a scan line length of **2048 bytes** (1024 pixels, 4096 scan lines). Unlike the figure in the section above, this describes a real graphics mode.

### Will this BIOS accept a scan line length it did not choose?

| Subfunction | Requested | Result | Length moved |
| --- | --- | --- | --- |
| `BL=02h` (bytes) | 2304 | status `0x004f`, 2048 bytes, 1024 pixels, 4096 lines | no |
| `BL=02h` (bytes) | 4096 | status `0x004f`, 2048 bytes, 1024 pixels, 4096 lines | no |
| `BL=00h` (pixels) | 1152 | status `0x004f`, 2304 bytes, 1152 pixels, 3640 lines | **yes** |

> **The length is settable, but only in pixels.** `4F06h BL=00h` changes it. `4F06h BL=02h`, which asks for the same thing in bytes, **returns success and leaves the length alone** — including when asked for a value it demonstrably can produce, since the pixel request above landed on exactly the byte count the byte request was refused for.

> A driver that sets the pitch through `BL=02h` on this BIOS will get a success code and an unchanged pitch. That is the failure most likely to present as a corrupted display and be mistaken for a driver bug. Use `BL=00h`.

> This took three attempts to establish. One request, in bytes, returned success without effect, and that is indistinguishable from a granularity the request did not meet. Reporting it as "not settable" would have been wrong, and was: the first run of this probe said exactly that.

Length after the attempts: 2048 bytes, restored: **yes**.

> `4F06h BL=03h` reported a *maximum* of 64 bytes per scan line while the current length was 2048. A maximum below the value in use is not a real answer, and the same subfunction left one of its output registers untouched. Treat its outputs as unreliable on this BIOS.

## What is not here

- `aperture.probe`: unsupported (no-aperture-probe-in-baseline)
- `edid`: unsupported (ddc-outside-baseline-contract)
- `runtime.driver`: unsupported (no-velocity9x-runtime)
- `survey.bios_data`: unsupported (no-dos-survey)
- `survey.platform`: unsupported (no-dos-survey)
- `vbe.controller`: unsupported (no-real-mode-vbe)
- `vbe.modes`: unsupported (no-real-mode-vbe)

## Files

- `descriptors/matrox-mga2064w-config-v1.json` (6169 bytes)
- `descriptors/tvp3026-on-mga2064w-v1.json` (6421 bytes)
- `evidence/aperture.ndjson` (8943 bytes)
- `evidence/baseline.json` (54101 bytes)
- `evidence/capture.ndjson` (20281 bytes)
- `evidence/config.ndjson` (5308 bytes)
- `evidence/modeprobe.ndjson` (8690 bytes)
- `evidence/probe-crtc.ndjson` (10406 bytes)
- `evidence/probe-gc.ndjson` (7353 bytes)
- `evidence/probe-seq.ndjson` (6667 bytes)
- `evidence/ramdac.ndjson` (8489 bytes)
- `evidence/report.md` (2727 bytes)
- `evidence/rom-capture.ndjson` (79941 bytes)
- `evidence/rom.bin` (32768 bytes)
- `evidence/vbe.ndjson` (12099 bytes)

## Operator notes

- Card was in its power-on VGA state throughout; no mode was set.
- No display attached to the card under test; console is on the RTX 2060 at 0000:01:00.0.

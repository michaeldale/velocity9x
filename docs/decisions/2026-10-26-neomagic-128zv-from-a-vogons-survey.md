# NeoMagic MagicGraph 128ZV (NM2093) from a VOGONS survey: tier 0 is plain VBE, the blitter sits at BAR0 + 2 MiB, and the BIOS hides 64 KiB

Date: 2026-10-26
Machine: none in the fleet. A 1996-97 Pentium notebook surveyed by a VOGONS
member; nothing below has run on it or on any NeoMagic part.
Evidence: one V9XSURV report (build d746481, `query-only`, 2026-10-25),
posted at <https://www.vogons.org/viewtopic.php?p=1452656#p1452656> and
committed unmodified as
[`neomagic-128zv-nm2093-vogons-vgasurv-2026-10-25-d746481.ini`](../probe/references/neomagic-128zv-nm2093-vogons-vgasurv-2026-10-25-d746481.ini).
The thread itself was not readable from here (WebFetch 403, browser pane
refused), so the poster and the notebook model are not recorded.

Driver sources checked, all fetched 2026-10-25:

- xf86-video-neomagic master `src/neo_driver.c`, `neo_reg.h`, `neo.h`;
- xf86-video-neomagic 1.2.9 `src/neo_2090.c` and `neo_driver.c` — master
  has dropped XAA, so the 2090/2093 blitter code survives only in old tags;
- Linux `drivers/video/fbdev/neofb.c`.

neofb was derived from the X driver, so where they agree that is one
source, not two.

## What the machine is

| | Value | Report section |
|---|---|---|
| Display | NeoMagic MagicGraph 128ZV (NM2093), `10C8:0003` rev 02 | `[PciDevice.0]` |
| BIOS | "MagicGraph 128 V/ZV 40K SVGA BIOS", (c) 1994-97, code rev 0109, VBE 2.0 | `[VideoBios]`, `[VBE]` |
| Panel | "Color 18-bit SVGA TFT": 800x600, 6 bits per gun | String.07 |
| Host bridge | `8086:1235`, Intel 430MX (Mobile Triton) | `[PciInventory]` |
| ISA bridge | `8086:1234`, 430MX MPIIX, class 0680 | `[PciInventory]` |
| PC Card | `1013:1100`, Cirrus CL-PD6729, class 0605 | `[PciInventory]` |
| OS at survey | DOS 7.10, no Windows running | `[System]` |

`CpuClass=286-or-later` is the d746481 tool's floor, not a measurement; the
430MX means a Pentium.

## Findings

1. **One BAR; the blitter is at BAR0 + 2 MiB.** BAR0 = `3FC00008`
   (memory, prefetchable); BARs 1-5 are zero; every LFB mode's PhysBasePtr
   is `3FC00000`. `neo_driver.c` (master, ~line 1055) puts MMIO at the
   linear base `+ 0x200000` for the NM2090/2093 (`+ 0x100000` for the
   NM2070); only the NM2097/2160 and later take MMIO from BAR1/BAR2. With
   `linearSize` 2048 KiB, BAR0 has to decode at least 4 MiB, which its
   4 MiB alignment fits. Here that is `3FE00000`. Neither the BAR size nor
   the registers were read: `query-only` does not size BARs.

2. **VBE reports 1088 KiB; both drivers say 1152.** X and neofb take
   `videoRam = 1152` for the NM2090/2093 from a per-chip table, not from the
   hardware. X reserves only 2 KiB (`CursorMem = 2048`) at the top for the
   cursor. So the BIOS withholds the top 64 KiB for a reason neither driver
   records. A driver should trust VBE's 1088 until a register read or a
   write-readback above 1088 KiB says otherwise.

3. **The mode budget rules out flips above 8 bpp.** LFB modes: 8 bpp to
   1024x768, 15/16 bpp to 800x600, 24 bpp packed at 640x480 only (X also
   lists 24 bpp for this chip, unaccelerated), no 32 bpp. 800x600x16 takes
   960,000 of 1,114,112 bytes and 640x480x16 takes 614,400: neither has
   room for a back buffer. Only 8 bpp can flip.

4. **6-bit DAC** (`Capabilities=00000000`), matching the 18-bit panel.

5. **No EDID.** `4F15` read returned `014F`. The panel size has to come
   from the chip: per `neo_driver.c`, after unlocking with GR09 = 26h, GR20
   bits 4:3 give 640x480 / 800x600 / 1024x768 / 1280x1024, GR21 bit 1 colour
   and bit 4 TFT (else dual-scan), GR20 bits 1:0 the LCD/CRT selection.

6. **The ROM unlocks the same way.** At ROM `01B4` the code reads GR09 and
   writes 26h, as X does (`VGAwGR(0x09, 0x26)`, then `0x00` to lock).
   `00B5`-`0154` read as a GR index/value table ending `FFFF`. The survey
   dumps only the 9 standard GR registers, so none of the extended state is
   in the report.

7. **No IRQ** (`InterruptLine=FF`), so retrace is polled. **Bus master
   off** (`Command=0003`), which is expected: the blitter is a slave.
   **Subsystem `0000:0000`**: an INF must match `PCI\VEN_10C8&DEV_0003`
   without `SUBSYS`. **ROM BAR 0**: the video BIOS is in the system image at
   C000 (40 KiB, checksum good).

8. **DPMS `0F10`** (VBE/PM 1.0, all four states); protected-mode interface
   present (208 bytes).

## The blitter (xf86-video-neomagic 1.2.9, `Neo2090AccelInit`)

- 8 and 15/16 bpp only; 24 bpp gets none.
- The pitch must be 640, 800 or 1024 pixels (`NEO_BC1_X_*` in BLTCNTL);
  any other width is refused. Off-screen surfaces therefore share the
  screen's pitch: the heap is whole lines below the visible area.
- Screen-to-screen copy is `NO_TRANSPARENCY | NO_PLANEMASK`. No
  colour-keyed blits; a DirectDraw source key would be software.
  `NEO_BC0_SRC_TRANS` appears only on mono colour expansion.
- Solid fill, overlapping copy (X/Y decrement), mono colour expansion.
- Registers (`neo_reg.h`): BLTSTAT 00h (bit 0 busy, bit 1 FIFO available),
  BLTCNTL 04h, FGCOLOR 0Ch, BGCOLOR 10h, SRCSTARTOFF 24h, DSTSTARTOFF 2Ch,
  XYEXT 30h (`h << 16 | w`); XY addressing; FIFO enable is BC3 bit 27.
- Hardware cursor registers at MMIO + 100h, 2 KiB image at the top of VRAM.

## Survey tool notes

- `PcirClassCode=000000` is the d746481 misread, not the ROM. The PCIR bytes
  at ROM `003C`-`003F` are `00 00 00 03`: revision 0, class `030000`. The
  current `tools/diag/vga_survey_dos.c` reads PCIR+0Dh and says so.
- `String.17`-`String.63` are the BIOS font read as text; they used up the
  string budget (`StringsTruncated=1`).
- What the report cannot settle — the panel bits, the hidden 64 KiB, whether
  BLTSTAT answers at `3FE00000` — needs a run that writes GR09. That is
  beyond `query-only` and needs the reporter's agreement.

## What it means for Velocity9x

- No NeoMagic family exists. One would be a new `neomagic` family for every
  NeoMagic chip, the per-chip differences (MMIO location, VRAM, cursor,
  blitter generation) as hw16 data.
- Tier 0 is plain: VBE 2.0, one linear BAR, 8/15/16 bpp at the panel's
  800x600, 6-bit DAC, polled retrace.
- DirectDraw: blits and fills on the 2090 blitter within the limits above;
  flips at 8 bpp only; no colour keys in hardware.
- No 3D engine: Direct3D, OpenGL and Glide would be the software paths.
- No work starts on this unless the reporter will run test builds; no fleet
  machine has the chip.

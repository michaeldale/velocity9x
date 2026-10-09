# Matrox Millennium family: tier-0, the 2D engine, then GDI

Date: 2026-10-09

Status: Phase 1 done 2026-10-09
([first boot](../decisions/2026-10-09-mga2064w-tier0-first-boot.md)):
enable-ok on the guest and on A8U4I5, 13 modes to 1600x1200 at 8/16/32 bpp,
GDI and the DirectDraw HAL passing; the monitor itself not yet looked at.
Phase 2 done 2026-10-09
([engine](../decisions/2026-10-09-mga2064w-drawing-engine.md)): the write
probe passes on silicon at 8/16/32 bpp, and DirectDraw fill and copy run on
the engine with correct pixels and no timeouts. Next: Phase 3, GDI.
Later the same day the Millennium II (`051B`) was folded in and `matrox-m2`
retired: one family, both chips, the BAR order per-chip data. Re-measured
on the guest and A8U4I5's 2064W afterwards; the 2164W has not run.

The MGA-2064W (`102B:0519`) is in A8U4I5 (10.0.1.172) since 2026-10-09,
replacing the Rage XL. Until now the chip sat in `matrox-m2`, a guarded
drop-in candidate with no INF, no DirectDraw HAL and one forced mode, which
never set a mode under Windows. This plan brings it to the standard the sis
and ati families meet: an INF package on the floppy, VBE modes merged from
the card's BIOS, the DirectDraw HAL, then the MGA drawing engine behind
DirectDraw and GDI. The chip has no 3D engine; Direct3D stays on the
software rasterizer.

## What is already true

- **The card is on Microsoft's inbox driver.** `MGAPDX64.DRV`/`.VXD` (DXMGA.INF,
  4-23-1999) bound at `Display\0011` on first boot, at 800x600x16. It is the
  baseline and the rollback, and it stays installed.
- **Resources** (Configuration Manager, A8U4I5): BAR0 `DC000000-DC003FFF`
  (MGABASE1, 16 KiB control aperture), BAR1 `DD000000-DD7FFFFF` (MGABASE2,
  8 MiB framebuffer). The reverse of the Millennium II.
- **The BIOS** advertises 20 linear-framebuffer modes and honours 4F06h
  only in its pixel form; 800-wide modes at 8 and 16 bpp are padded
  ([record](../decisions/2026-09-11-the-2064w-aperture-opens-with-mgamode.md);
  measured on the BringupKit host, possibly a different card).
- **CRTCEXT3 bit 7 (mgamode)** gates the full aperture. The BIOS's linear
  mode set turns it on: `MgaCrtcExt3=81` at 800x600x16 on A8U4I5.
- **An emulator exists.** 86Box `Win98SE-Millennium` (port 9877) models the
  2064W with its own ROM (different revision and pitches from the physical
  card, [record](../decisions/2026-09-10-the-2064w-in-a-guest.md)). It runs
  the cheap loop; the physical card is the gate.
- **Register reference.** MGA-1064SG Developer Specification (1997,
  `C:\everything\L10GL\docs\datasheets\MGA-1064sg_199702.pdf`), whose 2D
  core is the 2064W's ("the same Windows acceleration core", p.1-2). Every
  drawing register at `MGABASE1+1C00h`-`1CFFh` is write-only, and reads of
  it are not decoded (Table 3-4) - so engine state cannot be surveyed, only
  written and observed. The 2064W specification itself (10470-MS-0300) is
  cited by the BringupKit records but not on this host.

## Native baseline (2026-10-09, boot 354, MGAPDX64)

`V9XDDP` under Matrox's HAL at 800x600x16: `Result=COMPLETE`,
`GblHalVidMemTotal=0x00715A00` (7.1 MiB off-screen), engine fill
`BltFillPixelOk=1` in 1 ms, flips accepted. Kept in
`docs/probe/a8u4i5-mga2064w-native-2026-10-09/`.

## Phase 1 - tier-0 family `matrox`

Done in code 2026-10-09: `packaging/families/matrox/family.psd1`,
`include/velocity9x/matrox_mga.h`, `src/chipsets/matrox/mga_backend.c`,
`mga_hw16.c`, `mga2064w/mga2064w_hw16.c`. Every hw16 hook NULL,
`MiniVddVbeCollect`, seven static 8/16 bpp rows, floppy slot 4. The 2064W
left `matrox-m2`, which kept the Millennium II.

That split was a mistake: the library is organised by family, and two
families for one design split every fix between them. Corrected the same
day: the 2164W joined `matrox` as a second chip in one module,
`millennium/millennium_hw16.c`, whose engine hook maps whichever BAR does
not hold the framebuffer (BAR0 on the 2064W, BAR1 on the 2164W).
`matrox-m2`, its guarded drop-in package and its recovery and guard
tooling were removed, and with them the `V9X_TARGET_MATROX_MILLENNIUM2`
carve-outs in `dd16.c` and the Win16 loader probe.

Exit gate:

1. 86Box guest: `Stage=enable-ok`, `V9XGDI`, `V9XMSW` across the modes.
2. A8U4I5: installed by Update Driver from `C:\V9XMGA`, `Stage=enable-ok`,
   `MgaCrtcExt3` read, `V9XMODES.INI` showing the merged list and each
   mode's pitch (the 800-wide rows either corrected or refused at stage 9),
   `V9XGDI` pass, `V9XDDP` compared with the native run above.

## Phase 2 - the drawing engine for DirectDraw

- A host-tested builder, `src/chipsets/matrox/mga_engine.c`: register
  values for solid fill (`DWGCTL` TRAP + SOLID + ARZERO + SGNZERO +
  SHIFTZERO, `atype` RPL, `bop` 0Ch, `FXBNDRY`, `YDSTLEN`) and screen copy
  (BITBLT + BFCOL, `AR0`/`AR3`/`AR5` and `SGN` for the overlap direction),
  the pixel-unit addressing, and the 2064W's limits. Pure C, no I/O.
- Engine setup in the per-chip enable hook after every mode set: `MACCESS`
  `pwidth`, `PITCH`, `YDSTORG` 0, `PLNWT` all ones, clipper wide open.
  These are write-only, so the hook cannot check them; the write probe
  below is what licenses them.
- The mini-VDD maps BAR0's 16 KiB, as the SiS's BAR1 is mapped.
- `src/display32/engines/eng_mga.c`: fill and copy, a bounded wait on
  `FIFOSTATUS` free slots (`1E10h`) before each burst and on `STATUS`
  `dwgengsts` (`1E14h` bit 16) before the CPU touches the surface.

Before the engine path ships, a guarded write probe (`MGA2D.EXE`, the
SIS2D pattern) runs under the tier-0 driver: fill, forward copy and both
overlap copies at 8 and 16 bpp to off-screen VRAM, read back by the CPU.
It settles what the 1064SG document cannot for this chip: whether the
2064W needs `MACCESS` written at all after a VBE mode set, whether the
end coordinates are inclusive, and whether BLK (block mode) fills are safe
on this card's WRAM.

Done 2026-10-09 except BLK, which is untried: the setup is required, the
fill edge is exclusive and the blit edge inclusive. The engine state is
written by `eng_mga.c` on first validation per mode rather than by a 16-bit
enable hook, because it is MMIO-only and the HAL already holds the window.

## Phase 3 - GDI acceleration

`gdi_accel.c` has arms only for the S3 engines. Solid fill and
screen-to-screen copy first, then monochrome expansion for text
(`DWGCTL` ILOAD with `BMONOLEF` through the DMA window at `MGABASE1+0`).

## Later (sketch only)

- Page flipping: display start through `CRTC0C`/`0D` plus `CRTCEXT0<3:0>`,
  and the HAL's display-start capability.
- Hardware cursor in the TVP3026.
- A Millennium II run. First check: whether `V9XMINI.VXD` reproduces the
  August surface corruption that Matrox's own mini-VDD did not
  ([boundary](../specifications/matrox-millennium2-bringup.md)).

## Decisions fixed

- No clock, memory-controller or RAMDAC write: `OPTION`, `MCTLWTST`, the
  TVP3026 PLLs stay as the BIOS left them.
- CRTCEXT3 is read, not written: the BIOS sets mgamode.
- `V9XSTAGE` is not run under MGAPDX64
  ([issue](../issues/2026-10-09-preflight-disturbs-the-stock-matrox-driver.md)).
- `VideoMemoryBytes` stays the 2 MiB floor; the heap sizes from 4F00h.

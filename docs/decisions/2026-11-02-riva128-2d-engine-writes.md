# RIVA 128ZX 2D engine: fills and blits right, but never raw at 5:6:5

Date: 2026-11-02

Machine: M-7V636C, boot 11, the tier-0 `nvidia` package (`37d04471-dirty`).
Plan: `docs/plans/nvidia-riva128.md`, Phase 3 and decision 1. Tool:
V9XN32D.EXE with N32D.VXD (branch `nvidia-riva128`). Files:
`docs/probe/m7v636c-riva128-2d-2026-11-02/`.

## What ran

The first NVIDIA register writes on this card. For each depth, one op
list:

1. Read RAMIN's first RAMHT entry and first object word, PMC_ENABLE and
   PFB_CONFIG_0, to see what the previous run or a mode set left.
2. Save a 64 KiB scratch window of video memory 2 MiB in, fill its first
   10 lines (an 800-pixel surface at the desktop's depth) with a known
   pattern.
3. Linux rivafb's NV3 bring-up, in its order (riva_hw.c `LoadStateExt`,
   riva_tbl.h): `RivaTablePMC`, `RivaTablePTIMER`, PFB_CONFIG_0 for an
   800-wide surface, `nv3TablePFIFO`, RAMIN (RAMHT entries and the
   per-depth object words for ROP, clip, pattern, pixmap, blit, GDI
   rectangle and line), `nv3TablePGRAPH` and its per-depth SURF_CONFIG,
   then all four surface offsets at 2 MiB with the surface pitch. Left
   out: the DMA object and its page table, and the D3D object, which only
   3D uses; no CRTC or PRAMDAC register.
4. Bind channel 0's subchannels 0-6 as `RivaTableFIFO` does, wait for
   PGRAPH idle, latch the free count.
5. Clip to the surface, solid pattern, ROP CCh (rivafb
   `riva_setup_accel`), then two fills through the GDI rectangle object
   (40x5 at 8,2 and 20x5 at 60,2) and one blit (80x7 from 4,1 to 120,1,
   the source covering both fills), each followed by rivafb's `nv3Busy`
   wait.
6. Read the window back, read the interrupt and FIFO registers, restore
   the window.

The tool models the expected bytes and counts mismatching pixels in each
fill, in the blit destination and everywhere else. Three runs: 16 bpp
alone, then `/all` twice (16 bpp, 16 bpp with Y16 formats, then 800x600x8
and 800x600x32 through ChangeDisplaySettings, then back), the second with
a count that ignores the format's unused bits. The machine stayed up
throughout and the desktop came back intact at 800x600x16.

## Results

Every run: no PFIFO or PGRAPH interrupt, no timeout, PGRAPH idle and the
free count back at its latched 7Ch (31 entries) after each wait, CACHE1
PUT equal to GET, PMC_ENABLE `11111111` after the PMC writes. No pixel
outside the three rectangles changed in any run.

| Run | Fill A | Fill B | Blit | Masked (unused bits ignored) |
|---|---|---|---|---|
| 8 bpp | 0 bad | 0 bad | 0 bad | 0 |
| 16 bpp, rivafb's X1R5G5B5 | 200 bad: `7C1F` drawn `FC1F` | 0 bad (`F81F`, bit 15 set already) | 326 bad: `2483` copied as `A483` | **0** |
| 16 bpp, Y16 surface and objects | 200 bad: `7C1F` drawn `C210` | 100 bad: `F81F` drawn `FFFF` | 300 bad | 600 |
| 32 bpp | 200 bad: `00FF00FF` drawn `80FF00FF` | 100 bad: `FF123456` drawn `80123456` | 495 bad: `45274905` copied as `85274905` | **0** |

- **8 bpp is exact.**
- **16 bpp in X1R5G5B5 sets bit 15 of every pixel it writes**, fills and
  blit alike; the other 15 bits are right everywhere.
- **32 bpp changes only the top byte**: fills write 80h there, and a blit
  forces its bits 7:6 to 10b and keeps bits 5:0. The 24 colour bits are
  right everywhere.
- **Y16 is not a raw format.** It converts colour to luminance (`7C1F` to
  `C210`, `F81F` to `FFFF`). Dead.

Also measured:

- **A VBE mode set wipes RAMIN.** The RAMHT entry a 16 bpp run wrote read
  back `80000000` in the next run on the same mode, and `00000000` after
  ChangeDisplaySettings switched to 8 bpp, and again after the switch back
  to 16 bpp. The register probe's hypothesis is confirmed.
- **A VBE mode set leaves PMC_ENABLE and PFB_CONFIG_0 alone.** After the
  switch, PMC_ENABLE still read `11111111` and PFB_CONFIG_0 still held the
  previous run's value (`1219` at 8 bpp, `1119` at 32 bpp). The driver has
  to rewrite PFB_CONFIG_0 for the new mode, as it does RAMIN.
- PFB_CONFIG_0 as rivafb computes it, measured accepted: `1219` (800 wide,
  2 bytes), `1119` (1 byte), `1319` (4 bytes, field capped at 3).

## Overlapping blits

A fourth run, `V9XN32D /overlap` at 16 bpp (`V9XN32D-run4-overlap.INI`),
repeated the test with the 80x7 blit moved so source and destination
overlap: down-right (from 4,1 to 8,3), up-left (from 8,3 to 4,1) and along
one row (from 4,2 to 7,2, the case a plain left-to-right copier gets
wrong). The model treats each as a move, through a copy of the source.
**All three are right in every colour bit** (`MaskedBad=0`; the unmasked
counts are bit 15 again). The engine picks the copy direction itself, so
the driver needs no direction logic for screen-to-screen copies.

## Which surface is which

The runs above set all four PGRAPH surfaces alike. A fifth,
`V9XN32D /surfaces` at 16 bpp (`V9XN32D-run5-surfaces.INI`), put surface 0
two lines below surfaces 1-3. rivafb's surface objects give the
destination (class 58h, `NV3_SURF_DST` in envytools' graph intro) surface
index 0 and the blit source (59h, `NV3_SURF_SRC`) index 1. Modelled that
way, fills and the blit destination two lines down and the blit source
where it was, **every colour bit matched** (`MaskedBad=0`). So PGRAPH
surface 0 (offset 400630h, pitch 400650h) is the destination of fills and
blits, and surface 1 (400634h, 400654h) the blit source. A copy between
two DirectDraw surfaces sets the two apart.

## What this decides

- **Option A of decision 1 is dead.** A 5:6:5 desktop cannot take the
  engine's writes: bit 15 is the top bit of red there, and the engine sets
  it on every pixel. No raw 16-bit format was found (Y16 converts).
- **Option B works for the engine as measured.** With the desktop in
  5:5:5, bit 15 is unused and the engine's output is right in every
  visible bit. What B still needs is the display side: set the RAMDAC to
  5:5:5 after the VBE's 5:6:5 mode set, and tell GDI the desktop is
  5:5:5. PRAMDAC general control reads `00101100` under this BIOS's 5:6:5
  modes; rivafb writes `00100100` on NV3 and, on NV4, `00101100` only for
  16 bpp (`riva_hw.c` `CalcStateExt`), which points at bit 12 as the 5:6:5
  select. Not yet written or watched on a monitor; an agent screenshot
  cannot show it, because it reads video memory, not the DAC.
- **32 bpp is usable for the desktop as is**: X8R8G8B8 ignores the top
  byte. A DirectDraw surface whose top byte is alpha (an ARGB texture) must
  not be blitted by the engine.
- **Every mode set needs the full bring-up again**: RAMIN, and with it the
  objects, is gone, and PFB_CONFIG_0 is stale.

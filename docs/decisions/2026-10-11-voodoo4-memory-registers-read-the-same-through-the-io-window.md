# The Voodoo4's memory registers read the same through the I/O window

Date: 2026-10-11. Machine: A8U4I5, boot 409, Velocity9x `3dfx` tier-0 at
640x480x16 (BIOS mode 111h). Card: retail Voodoo4 4500 AGP, `121A:0009`
subsystem `0004121A`. Evidence:
[VSA100-boot409-io-vs-mmio.TXT](../probe/a8u4i5-voodoo4-velocity9x-2026-10-11/VSA100-boot409-io-vs-mmio.TXT).
Tool: `VSA100.EXE` + `VSA100.VXD` with the BAR2 reads added. Plan:
[3dfx-vsa100-family.md](../plans/3dfx-vsa100-family.md), Phase 2 step 3.

## Why it was measured

The memory hook runs in the 16-bit driver before anything is mapped, so it
can reach the chip only through port I/O. The Napalm spec (section 4) says
BAR2 mirrors the init registers. Glide's `h3regs.h` says the tile control
and compare registers do not exist in I/O space. One of the two is wrong for
this purpose, and the hook depends on which.

## Measured

- BAR2 at `D000`, the Config Manager I/O range, equal to the live BAR.
- Every init register in 04h-4Ch that the probe also read through BAR0
  (15 of them) read the same value through BAR2. Status (00h) was left out of
  the comparison because it can change by itself.
- The `lfbMemoryConfig` selector protocol through BAR2: plain read `3FFF`,
  control `3FFF`, compare 0, selector written back to control. Through BAR0,
  read afterwards: the same three values. Not ambiguous either way.
- `dramInit0` `581EA9A9` through both: 32 MiB.

## What this shows

The tile registers do exist behind `lfbMemoryConfig` in I/O space, as the
spec says; Glide's comment does not hold for reads through the selector.
The 16-bit driver may read the memory straps and the linear/tiled boundary
through BAR2 before any mapping. In the BIOS mode the plain read returns
tile control, so the selector the hook restores is control here.

## The hook built on it (boot 410)

`V9xTdfxReadMemoryRegs` in `runtime.asm` does the same reads through BAR2;
`v9x_tdfx_read_video_memory` in `tdfx_hw16.c` decides with
`v9x_tdfx_vsa100_usable_memory`. Deployed by WININIT rename
(`V9XDISP.DRV` 47,474 bytes), boot 410:

- `Stage=enable-ok`. `V9XHW.INI`: `DramInit0` 1478404521 (`581EA9A9`),
  `TileFirstPage` 8191 (`1FFF`), `VramUsableBytes` 33550336.
  `V9XMODES.INI`: `Vram=reported=16777216 usable=33550336`.
- DirectDraw heap `GblHalVidMemTotal` = `01F69000` (32,935,936), the usable
  bytes less the 614,400 visible. `V9XDDP`: `Result=COMPLETE`, fill, copy
  and all four overlap cases pixel-correct; `FlipPixelOk=0`, the
  long-tracked GDI-readback-after-flip result.
- `V9XGDI /auto` PASS.
- After the enable, the probe read the selector on control (plain read
  `3FFF`), as the BIOS had left it.

## Not established

Writes through BAR2 other than the selector. Any board but this one.
Allocations that reach the top of the heap (`V9XDDP` does not fill it).

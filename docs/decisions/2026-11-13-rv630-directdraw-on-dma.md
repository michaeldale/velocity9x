# DirectDraw fill and copy on the HD 2600 XT's DMA engine: V9XDDP passes, the ring drawn through

Date: 2026-11-13

Status: **measured, one card, one boot.** M-NZHWBR's HD 2600 XT
(`1002:9588`, RV630), boot 5, the tree's `ati` package (`0.49.0
9160f2ff-dirty`, the commit this record goes in with) installed over the
`vbe` driver key by WININIT.INI renames and a `reset`. Step 3's second
half in [the plan](../plans/ati-rv630-hd2600xt.md). Captures:
[`../probe/rv630-directdraw-2026-11-13/`](../probe/rv630-directdraw-2026-11-13/).

## What went in

- `r6_dma.c` (pure, host-tested by `test_r6_dma.c`): a DirectDraw fill is
  a seed row the CPU writes and a COPY a row from it; a copy is a COPY a
  row, or one when the rows are contiguous; bottom-up when an overlapping
  destination is below its source. Rows of one dword, anything off
  dwords, and an overlap between two pitches are declined to the CPU: the
  write probe's three rules
  ([record](2026-11-13-rv630-dma-write-probe.md)). The test runs every
  plan through two engine models, in order and with every read stale
  inside a submission; making the fill copy each row from the row above
  (the shape that failed on the card) fails it in seven places, and was
  put back.
- `eng_r6.c`, engine type 15 (`ATI_R600`): Evergreen's ring in the same
  window at BAR0 + 240 MiB, reset and started once per mode without
  writeback or interrupts. Every operation first waits for the engine to
  finish the last; idle is the read pointer at the write pointer, the last
  sequence fence landed, and `DMA_STATUS_REG` bit 0 set.
- The mini-VDD's `EG_MAP` takes R6xx's `CONFIG_MEMSIZE` in bytes beside
  Evergreen's in MiB; the ranges cannot meet.
- The row sits in `cedar_hw16.c`, on Evergreen's hook with another engine
  type: a module of its own took the `ati` driver's `_TEXT` 789 bytes past
  its 57,344-byte budget, the shared hook 49. `d3dmode.c` moved to
  `LEAFCODE` behind far entries (`leaf16.h`); every call to it in
  `dd16.obj` and `ddi.obj` disassembles as `CALL FAR` (`9A`), and `_TEXT`
  is 57,149 bytes.

## Boot 5

- `V9XBOOT.INI`: `Stage=enable-ok`, 1024x768x16, aperture BAR0
  `E0000000`.
- `V9XHW.INI`: `Adapter=ATI Radeon HD 2600 XT (RV630)`,
  `EvergreenEngine=claimed`, BAR0 `E0000000`, BAR2 `F7E20000`, map reason
  0, `EngineCaps=00000003` (fill, copy). `ScreenShadow=on`.
- All eight installed files hash as the package; the staged copies are
  gone.

## V9XDDP

Three runs, the desktop at 1024x768 in 16, 32 and 8 bpp. The probe sets
its own 640x480x16 mode each time (`AfterModeBpp=16`), so all three
measured 16 bpp:

| | Run 1 | Run 2 | Run 3 |
|---|---|---|---|
| BltFill | 1 | 1 | 1 |
| SrcCopy | 1 | 1 | 1 |
| KeySrcFill | 1 | 1 | 1 |
| Overlap down / right / pitch-down / pitch-right | 1 / 1 / 1 / 1 | 1 / 1 / 1 / 1 | 1 / 1 / 1 / 1 |
| Flip | 0 | 0 | 0 |

`FlipPixelOk=0` is expected: the row claims no flips, and the 23 flips
were declined (`FlipDeclined=23`).

## The engine did the work

`V9XTRACE`: `EngineType=15`, `EngineCaps=3`, both builds `9160f2ff-dirty`
and agreeing, `EngineIdleTimeouts=0`, `EngineResets=0`, no other timeout.
V9XRVMM read after run 1 and again after run 3: `DMA_RB_CNTL=1B`
(enabled, 8,192 dwords), `DMA_RB_BASE=00EF0000` (GPU `EF000000`),
`DMA_RB_RPTR` = `DMA_RB_WPTR` = `3800h`, DMA idle. 14 KiB of packets
went through a ring that validation alone advances by 64 bytes, so the
probe's fills and copies were the engine's. The same pointer after run 3
as after run 1 fits the ring being restarted at each mode set, with the
same work after it; that was not checked further.

## A regression on this machine

Under `vbe` the card had software Direct3D (`Direct3DMode=software`,
`Direct3DDefault=2`): the `vbe` family is built with
`V9X_D3D_DEFAULT_REQUEST=2`. The `ati` family is not, so boot 5 reads
`Direct3DMode=none`, `Direct3DDefault=0`. The owner's 3DMark 2000 and 2001
SE shortcuts would find no Direct3D. Not changed here; it needs a decision
for the family.

## Not measured

- Fill and copy through the HAL at 8 and 32 bpp: V9XDDP draws at 16 only.
- Any game or benchmark; GDI and mode cycles (`V9XGDI`, the mode matrix).
- What the monitor shows.
- Throughput, and what the wait before every operation costs.
- Write combining, a larger heap, flips, Direct3D.

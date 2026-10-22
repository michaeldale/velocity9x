# Ironlake flips: Gen4's plane write, without the Gen3 status registers it lacks

Date: 2026-10-22
Status: **measured on one machine, one DirectDraw session.** Follows
`2026-10-22-ironlake-igma-2d.md` on W9QI2F4F (`M-J0F0SJ`). Captures:
`docs\probe\w9qi2f4f-ironlake-igma-2026-10-22\boot48-*`.

## Why the GM965's flip could not simply be switched on

The HAL's Gen3 scanout (`i9xx_scanout.c`), with the GM965's I9XX_GEN4
bit, writes the plane through DSPxLINOFF and DSPxSURF and reads the live
pipe's DSL, VTOTAL and frame counter. Ironlake keeps the plane, pipe
timing and DSL at those offsets (PRM Vol 3 Part 2 3.1 and 4.1). Around
the flip the same path also touches registers Ironlake does not have:

| Access | Gen3/Gen4 | Ironlake |
|---|---|---|
| ISR 20ACh, read at every flip and in the completion test | the flip-pending bit | not in the PRM; display interrupts are DEISR at 44000h |
| PIPExSTAT 70024h/71024h, **written** once a session to clear the underrun bit | FIFO underrun status | not in the PRM |
| FW_BLC 20D8h, DSPARB 70030h, read for watermarks | the watermark registers | WM0 to WM3 at 45100h instead |
| PFIT 61230h, LVDS 61180h, VGACNTRL 71400h, in the per-flip snapshot | display layout | the panel fitter, ports and VGACNTRL moved (68080h, E1180h, 41000h) |
| 70040h with 70044h's low byte | a 24-bit count split across two registers | 70040h is the whole 32-bit PIPEx_FRMCOUNT, 70044h the flip counter (p151) |

## What changed

- `V9X_DD_ENGINE_CAP_I9XX_GEN5` (`engine_abi.h`), stamped by the Ironlake
  descriptor only. The descriptor now keeps FLIP; FLIP_RING stays
  withheld, because the ring flip completes on the Gen3 ISR bit.
- `i9xx_scanout.c` on that bit: the plane base is Gen4's SURF and LINOFF;
  the frame count is one read of 70040h; the ISR reads, the PIPESTAT
  baseline and its write, the watermark path and the layout snapshot are
  skipped (the last three moved, unchanged, into
  `v9x_i9xx_note_gen3_status`). The vblank test, the coverage sampler and
  the issue-line statistics read only DSL, VTOTAL, PIPESRC, DSPCNTR and
  the stride, all present.
- `V9XTRACE.EXE` skips its live-register list on this bit
  (`LiveRegError=gen5-layout`) instead of reading the Gen3 offsets.

## Boot 48: V9XDDP

The new `V9XDISP.DRV` and `V9XHAL.DLL` by WININIT rename, then `reset`;
desktop at 1024x768x32. `V9XDDP` ran its exclusive 640x480x16 session
(`boot48-ddp-V9XDD.INI`); `V9XTRACE` from this branch afterwards
(`boot48-ddp-V9XSNAP.INI`):

- `EngineCaps=00100404`: FLIP, ASYNC_SUBMIT and I9XX_GEN5.
- `FlipHr=0`, **20 flips in 323 ms, longest 17 ms** - one per 60 Hz frame.
  The Ivy Bridge's same probe read 327 and 17.
- `FlipHandled` 23, **`FlipTakenAtDone` 23, `FlipNotTakenAtDone` 0**,
  `ScanoutUnresolved` 0, `FlipDeclined` 0.
- `FlipBaseImmediate` 25, `FlipBaseDeferred` 0: SURF plus LINOFF read back
  the new base at once on every write.
- Issued at lines 208 to 243 of 480, inside the write window.
- The skipped paths stayed skipped: `PipestatCleared` 0, `IsrAfterFlipOr`
  0, `WmWrites` 0, `ScanLayoutSamples` 0. `FrameCoverRecords` 1.

`FlipPixelOk=0`, as on the Ivy Bridge's run of the same probe.

## Not measured

- Whether the screen showed the flipped buffers. The counters say the
  plane base moved and the frame counter passed; nobody watched the CRT.
- FIFO underruns. DEISR is not read.
- Flips at 8 or 32 bpp, at other modes, and in a game.

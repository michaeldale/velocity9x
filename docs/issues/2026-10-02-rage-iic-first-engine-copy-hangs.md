# The Rage IIC's first engine copy hung A8U4I5

Priority: high. On the first build that claimed the Rage IIC's 2D engine,
the first DirectDraw screen copy left the machine needing a hands-on
restart.

Date: 2026-10-02. Machine: A8U4I5, ATI 3D Rage IIC AGP `1002:4757` rev
`7A`, boot 132, `dc67d29` (Phase 1 of
[ati-rage-iic-hardware-3d.md](../plans/ati-rage-iic-hardware-3d.md)).
Evidence: [`../probe/a8u4i5-rage-iic-registers-2026-10-02/`](../probe/a8u4i5-rage-iic-registers-2026-10-02/)
(`HANG132-*`, `ATIIC-BOOT134-V9X-BLOCK0.TXT`).

## What happened

`V9XDDP.EXE`'s write-through step log (`HANG132-V9XDDT.TXT`) got this far:

- the colour fill, on the engine, completed in 38 ms with pixels correct
  (`BltFillPixelOk 1`);
- the back-buffer and primary fills completed;
- 20 flips took 1 ms;
- the screen-to-screen copy was submitted: `SrcCopyBltHr 0`.

Nothing after that was logged. The next step locks the destination to
read the pixels back. From then on the agent did not answer, and its port
was refused for over six minutes while the machine still answered ping.
Michael restarted it by hand (boot 133). The `V9XTRACE.INI` on the
machine is a stale S3-era file, so there is no HAL trace.

Recovery: at boot 134, `fc1ed35`'s `V9XDISP.DRV` replaced only the 16-bit
driver, by a single WININIT rename. It stamps no engine, so the HAL
never reaches the engine path. Measured: `Acceleration=none`.

## What the engine looks like without ATI's driver

`ATIIC` (read-only) at boot 134, under Velocity9x, against its run under
ATI's driver earlier the same day:

| Register | Under ATI's driver | Velocity9x, boot 134 |
|---|---|---|
| `SRC_CNTL` (`+5B4`) | `00000040` | `00007EA3`: SRC_PATTERN_EN, SRC_ROTATION_EN, SRC_TRACK_DST, COLOR_REG_WRITE_EN and BLOCK_WRITE_EN set |
| `SRC_HEIGHT1` (`+594`) | `00000008` | `000057DA` |
| `PAT_CNTL` (`+688`) | `00000000` | `00000005` |
| `CONTEXT_MASK` (`+720`) | `FFFFFFFF` | `3D7DDBFA` |
| `GUI_TRAJ_CNTL` (`+730`) | `00400023` | `3523674C` |
| `GUI_STAT` (`+738`) | `00C00000` | `00800000` |

Most engine registers hold a repeating `...D7DA` pattern, which looks
like power-on contents. Nothing initialises the engine before ours runs.
On the Gateway, ATI's driver and BIOS evidently had, and none of the
Mobility work ever wrote these registers.

## Hypotheses, none yet confirmed

1. **Stale `SRC_CNTL`.** The copy builder never writes it. With source
   pattern, rotation and block write enabled on an SDRAM board, a source
   read could fail to terminate. The fill reads no source, which fits it
   passing. atyfb and X.Org both write `SRC_CNTL = SRC_LINE_X_DIR` and the
   rest of a known state at init.
2. **The FIFO model.** Phase 1 used the VTB+ model, which takes
   `GUI_STAT[25:16]` as a free count: 192 at idle. atyfb drives the
   264GT2C with the 16-entry `FIFO_STAT` check, and the RRG puts the 3D
   RAGE's encoded count at "less than or equal to 32". Trusting 192 could
   overrun the FIFO. The copy and its 2D-mode reset are 16 writes queued
   without an intervening check, exactly one 16-entry FIFO.
3. **The pre-VTB idle wait** returned on an empty `FIFO_STAT` without
   checking `GUI_ACTIVE`. That was not used in `dc67d29` (VTB+ model),
   but it would have been under hypothesis 2's fix. Fixed with a failing
   host test first.

## The next build

It changes only what these hypotheses name, writing no new registers
beyond the known state:

- `v9x_m64_build_engine_init_gt`: atyfb's `aty_init_engine` state, 18
  registers, written once at validate, in chunks of 8;
- the pre-VTB, 16-entry `FIFO_STAT` model for `ATI_RAGE2`;
- a pre-VTB idle that also waits for `GUI_ACTIVE`.

Untested until it runs. If it hangs again, the next step is a private
scene that records every write before executing it, not another
DirectDraw run.

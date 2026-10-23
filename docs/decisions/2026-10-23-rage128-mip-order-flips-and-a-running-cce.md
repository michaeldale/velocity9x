# Rage 128: mip offsets smallest-first, flips through CRTC_OFFSET, and the CCE left running

Date: 2026-10-23

Status: **measured, one card.** A8U4I5's Rage Fury Pro, boots 527 to 530.
This follows the [Direct3D record](2026-10-23-rage128-direct3d-through-the-cce.md).
Evidence: [`../probe/a8u4i5-rage128-d3d-2026-10-23/`](../probe/a8u4i5-rage128-d3d-2026-10-23/).

## 1. 3DMark's textures were garbage: the offset registers count up from the smallest level

V9XD32 passed in boot 527, but Michael's OBS recording of 3DMark 2000
Game 1 showed:

- the distant terrain striped;
- trees as vertical stacks of copies;
- rotor blades smeared;
- a rainbow block of unrelated memory in a corner.

See `obs-game1-mip-order-boot527.png`. V9XD32's only texture is a single
8x8 level, so it never exercised a mip chain.

The builder wrote level *i* to `TEX_OFFSET_C` register *i*, base first.
Mesa's `r128UploadTexImages` writes them the other way:
`tex_offset[numLevels - 1 - i] = level i`. With a full chain, the sampler
was therefore reading the 1x1 level's memory as the base. The builder now
writes the Mesa order, and registers past the chain hold the base. The
test was changed first and failed (`test_r128_3d.c:159`, `161`, `162`).

After the fix, Michael reported "looks good now" from the monitor (boot
528). I have no recording of that run. Whether this one change accounts
for all four symptoms is not shown separately.

## 2. Flips: CRTC_OFFSET, latched at the blank

Michael then reported the remaining problems as poor performance and
page/flip flicker. The boot-527 counters show the cause:
`FlipDeclined=77,448`, `FlipHandled=0`. The Rage 128 had no scanout path,
so the HAL declined every flip and DirectDraw presented by copying on the
CPU.

The Pro register guide (RRG-G04500 pp. 3-66 to 3-73) gives three facts:

- `CRTC_OFFSET` (0x224) is the base in bytes, with bits 2:0 zero.
- With `CRTC_OFFSET_CNTL`'s `CRTC_OFFSET_FLIP_CNTL` clear, a new offset
  is used at the vertical blank.
- `CRTC_GUI_TRIG_OFFSET` (bit 30) reads 1 from the write until display
  starts at the new address.

A8U4I5 read `CRTC_OFFSET_CNTL = 0` under both drivers (V9XR1MM, boots 519
and 520). The flip is therefore written at any line and is pending while
bit 30 is set, which is the same model as Haswell's plane.
`r128_crtc.c` (host-tested) holds the arithmetic and `r128_scanout.c`
holds the register access. A control word that is tiled, per-line or
locked reports the scanout as not live, so flips are declined as before.

Boot 529, 3DMark 2000:

- `FlipHandled=5,176`, `FlipTakenAtDone=5,175`, none declined;
- the three buffers rotate at `0`, `0x180000` and `0x300000`;
- the score was **995** (993 before).

Boot 530: Michael watched Game 1 on the monitor and saw no flicker.

## 3. The CCE left running between batches

Until boot 529, every batch started the CCE, waited for it to go idle,
flushed and stopped it. The CPU could not build a batch while the engine
drew one.

The CCE now lives in `engines\r128_cce.c`. A batch starts the CCE if it is
stopped and leaves it running. `v9x_r128_cce_drain` waits, flushes and
stops it, and is called from:

- `eng_r128.c`'s `wait_idle`, which Flip, Lock, GetBltStatus,
  DestroySurface and the core's CPU clears reach through
  `v9x_render_drain`;
- the 2D fill and copy, whose direct register writes must not meet a
  running microengine.

The MGA engine has worked this way since 2026-10-10.

The microengine is never left running with nothing to drain it. The
boot-522 hang was a probe that exited with it free-running, followed by
our 2D engine writing registers.

Boot 530, 3DMark 2000:

- score **1105**;
- 44,367,363 triangles in 1,015,802 batches;
- `FlipHandled=5,466`, every one taken;
- no idle or FIFO timeouts, the engine still valid at the end;
- V9XD32 passes at 32 and 16 bpp.

ATI's driver scores 2404 on the same card. These scores are recorded, not
investigated.

## What is still PIO

Every dword of every batch is a CPU write to `PM4_FIFO_DATA_EVEN/ODD`:
about 1,100 dwords for a 44-triangle batch, plus about 50 dwords of state
re-sent with each batch. ATI's own driver runs the CCE from an AGP ring
(register probe, 2026-10-22). A bus-master ring is the next submission
step in the plan's decision 1. It has not been started.

# RS690 2D write probe: fills and copies byte-exact at 8, 16 and 32 bpp, first run

Date: 2026-11-04

Status: Phase 3 of [the plan](../plans/ati-rs690-x1200.md), first step,
passed. Machine M-GB8KRE (ATI RS690 IGP, `1002:791F`), signed 0.34.0
`vbe` driver. Probe `V9XRS2D.EXE` + `RS2D.VXD`, built from branch
`ati-rs690` at `cc8a522e` with the builder and probe uncommitted (build
id `cc8a522e-dirty`); the commit that adds them carries the same source.
Captures: [`../probe/rs690-2d-write-probe-2026-11-04/`](../probe/rs690-2d-write-probe-2026-11-04/).

## What ran

The probe writes, register for register, what the pure builder
`src/chipsets/ati/rs690/rs690_2d.c` returns, and compares the result with
a C model of the same operation, byte for byte. The builder follows
xf86-video-ati 6.14.6's MMIO EXA path: `Emit2DState`'s eight state
registers, then `DST_PITCH_OFFSET` (and `SRC_PITCH_OFFSET` for a copy),
the coordinates, and `DST_HEIGHT_WIDTH` last, which starts the operation.
Pitch-offsets carry the GPU address: `HDP_FB_LOCATION`'s `78000000` plus
the LFB offset, in 1 KiB units.

Before touching the engine it checked `CP_CSQ_CNTL` (0: CP off), read the
frame buffer base, and wrote the init list (`DP_DATATYPE` with the host
big-endian bit cleared, the default scissor at its maxima, the write
mask). Every test then: prefill a 4 KiB surface at LFB offset 16 MiB
(above everything the `vbe` driver uses), wait for idle, the builder's
writes, wait for idle, read back. Wait-for-idle is `RBBM_STATUS` (64 FIFO
entries free, bit 31 clear), then a `DSTCACHE_CTLSTAT` flush polled to
not busy, as `RADEONWaitForIdleMMIO` does on R300 and later.

| Test | What it checks |
|---|---|
| Fill | 24x10 solid fill |
| FillUnaligned | the same 16 bytes past a 1 KiB boundary; the builder moves the remainder onto x |
| CopyForward | disjoint copy |
| CopyRight | overlap 4 pixels right: right to left |
| CopyDown | overlap 3 rows down: bottom to top |
| CopyDownRight | both reversed |

## Results

| Desktop (`V9XMSW /set`) | Surface pitch | Every test | Mismatching bytes |
|---|---|---|---|
| 1024x768x16 | 128 | PASS | 0 |
| 1280x1024x32 | 256 | PASS | 0 |
| 640x480x8 | 64 | PASS | 0 |

- The engine was idle at the start every time (`RBBM_STATUS`
  `10000140`). The wait before each operation took 2 reads; the wait
  after, 5 to 11. No wait timed out.
- No reset, clock setup or CP work was needed: the 2D engine works as
  VBE leaves it, the first of the plan's unknowns.
- The desktop came back at 1024x768x16 and a screenshot after the run
  showed it intact. That capture is not kept: it shows the owner's chat
  window.
- A no-op engine could not have passed: every model differs from the
  prefill in the drawn rectangle.

## Not measured

- Throughput, and what a fill or copy costs against the CPU on the
  uncached LFB.
- The desktop surface itself (the probe draws only off-screen), and
  whether anything reaches the monitor.
- Behaviour across a DOS box or a mode set in the middle of an operation.

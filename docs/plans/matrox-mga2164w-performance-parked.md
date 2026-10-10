# Millennium II Direct3D performance: what landed, and what is parked

Date: 2026-10-11. Status: parked. Michael parked both follow-ups below on
2026-10-11; the timing instrument stays in the driver.

Machine: A8U4I5 (P3 1002 MHz, 440BX, MGA-2164W 8 MiB). Benchmark: Half-Life
1.1.1.0 `timedemo mwd5`, Direct3D, 640x480 full screen, best of three
([procedure](../../tests/benchmarks/hl1/README.md)), run by Michael.
Record: [Direct3D engine](../decisions/2026-10-10-mga2164w-d3d-engine.md).

## Landed

| Change | Best mwd5 | Commit |
|---|---|---|
| Lightmaps at the half stipple; perspective s, t, q scaled together (boot 392) | 8.391 | `aba41c9` |
| No end-of-batch idle wait; FIFO credit (boot 396) | 8.623 | `22f365d` |
| Per-stage timing on the MGA where the CPU has a TSC, with the trace folding of `dd9c44f` (boot 397) | 8.492 | this plan's commit |

Tried and dropped: a cache that skipped state registers already holding
their value. Its lookups cost about 8 % (7.953 with it, boot 393; 7.773
with it and the idle wait, boot 394).

## Where the time goes (boot 397)

The HAL's TSC buckets on the MGA (`d3d_mga.c`; the names are the Gen3
ring's), over the three runs, at 1002 MHz:

| Bucket | Meaning on the MGA | Seconds | Share of the batch |
|---|---|---|---|
| RingWait | polling FIFOSTATUS for room | 103.6 | 64 % |
| Decode | triangle setup and register builder | 35.7 | 22 % |
| RingWrite | the register writes | 16.4 | 10 % |
| rest | mapping, texture resolve, bookkeeping | 5.1 | 3 % |
| EngineDraw | the whole batch | 160.8 | 100 % |

Every one of the 5,104,342 trapezoids found the FIFO short of room
(`TimeRingWaitCalls` equals `TimeRingWriteCalls`). The drawing engine is
the limit; the CPU's setup mostly overlaps it. Per frame, the clear
(BltFill) is 3.3 ms, under 3 %. Snapshots: A8U4I5 `V9XSNA7.INI` before and
after, not kept in the tree.

Ruled out by this: dropping FIFOSTATUS reads and relying on the chip's
PCI retry when the FIFO is full (2164W spec, section 4). The CPU would
stall on retries instead of reads, against the same engine, and a stalled
engine would hard-lock the machine rather than record a timeout.

## Parked

### 1. What perspective costs the engine

Every textured triangle whose three 1/w differ at all is drawn with
perspective (`d3d_mga.c`), which divides at every pixel. If the 2164W
fills slower so, affine texturing where 1/w barely varies (a small ratio,
or a small triangle) would draw the same texels for less engine time.

First step, no driver change: time one large trapezoid in MGA2D with
TEXCTL.npcen set and clear, by TSC from the last register write to
STATUS idle, at several sizes. Then, only if perspective is markedly
slower, choose a threshold where affine's error stays under half a texel
(the host test in `test_mga_setup.c` can measure that error) and measure
mwd5.

### 2. What the lighting stipple costs the engine

The lightmap pass (DESTCOLOR/SRCCOLOR) is drawn at the half stipple. It
writes half the pixels, but the engine may still walk, texture and
Z-test all of them, which would double every lit surface's fill time.
Same MGA2D timing: a textured, Z-tested trapezoid at trans 0 and trans 1.
If the stipple costs a full pass, compare mwd5 with lighting refused
(8.391's lighting build against an unlit one) so Michael can choose.

### 3. The triangles setup declines

Boot 397 declined 39,194 triangles at the trapezoid split
(`MgaSetupRefusedSplit`), about 34 a frame: a plane left its field
(colour step past 256 levels a pixel, Z past 17.15, or s, t, q past 32
bits). Whether they are slivers or visible holes is not known. Next: a
counter per plane, or a host replay of a refused triangle's vertices,
then decide whether a fallback scale (as the perspective retry already
does) covers them.

### Also seen, not investigated

One blend is refused per frame (`M64Policy04` 1,164 over 1,164 frames),
most likely a full-screen effect.

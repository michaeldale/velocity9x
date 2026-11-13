# GT218 502D measurements for the HAL engine (step 4.0)

Date: 2026-11-14

Step 4.0 of [docs/plans/nvidia-gt218-nv50-engine.md](../plans/nvidia-gt218-nv50-engine.md):
what the engine may assume, measured with the probe on M-BXMFA8 (GT218
A8, boot 106) before any HAL code depends on it. Items are added as they
are run. All runs: `V9XN5CH /flush:none /push:class02 /pusher`. Logs in
`build/driver-results/m-bxmfa8-gt218-boot84/`.

## 1. Copy, non-overlapping, 32 bpp: passes (run 33)

The probe at 5ce5e33e (built as `f1433f74-dirty`). After the fifo, gr and
fill stages passed as in run 32, IB entry 3 carried
`v9x_nv50_push_copy`: a 16x16 block at (0, 0) of the 128x72 X8R8G8B8
test surface, which straddles the filled rectangle's corner at (8, 4)
and so holds both pattern and colour, copied to (100, 50). Log:
`run33-V9XN5CH.LOG` (from line 9893).

- IB get 4 = put 4, DMA get = put = `080223AC`, pusher error 0.
- PGRAPH_INTR 0, trap 0, idle; PMC_INTR 0, PFIFO_INTR 0; no master
  abort; the BAR1 marker intact.
- Read back through BAR1, top byte excepted: **256 of 256 copied pixels
  hold what the source held, 8960 of 8960 others are unchanged.**

So the copy push is right as written for this case: the source run at
230h and 244h, safe overlap 1, **blit control 0** (point sample, centre
origin), and the twelve words from 8B0h in the order destination
rectangle, du/dx and dv/dy as (fraction 0, integer 1), source x and y as
(fraction 0, integer). The control value and the order were recalled from
nouveau's X driver, not in the saved sources; this run is their
evidence. A copy with centre origin lands exactly, with no half-pixel
shift.

## 2 and 3. Overlapping copies, and 32, 16 and 8 bpp: all pass (run 34)

The probe at 43c16bd3 (built as `4dd7a97b-dirty`). After run 33's
stages, a test per depth on a 128x72 surface of its own: a CPU gradient
(every pixel differs from its neighbours), an engine fill of 64x64 at
(8, 4), an overlapping copy down and right (32x32 from (16, 16) to
(24, 20)), and one up and left (40x30 from (60, 30) to (50, 24)), a kick
each. The same operations ran on a model in memory whose copy reads
the whole source first. Log: `run34-V9XN5CH.LOG` (from line 10550).

| Depth | 502D format | Fill colour | Pixels matching the model |
| --- | --- | --- | --- |
| 32 bpp | E6h, X8R8G8B8 | `00A5C3E1` | 9216 of 9216 |
| 16 bpp | E8h, R5G6B5 | `A5C3` | 9216 of 9216 |
| 8 bpp | F3h | `5D` | 9216 of 9216 |

Every one of the nine kicks: PGRAPH_INTR 0, trap 0, idle, pusher error
0. IB get = put = 13 at the end; the machine up.

So, for the engine:

- **An overlapping copy is correct in both directions** with safe
  overlap set, with the same push; the engine need not order or split
  it.
- **16 bpp R5G6B5 is exact**, the full 16 bits (the fill colour has bit
  15 and bit 5 set). The RIVA 128's bit-15 problem does not exist here.
- **8 bpp works with format F3h** for the surface and the solid colour;
  the value is written as given.
- The destination and source formats can be the same value in one push.

## Not yet run

4. Several hundred kicks, to wrap the IB ring.
5. A mode set with the channel up.
6. The push DMA object in class 3Dh.

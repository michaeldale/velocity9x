# The Gen3 head wait is pixels: about 10 Mpixels/s, and a fixed batch cost of 5 us

2026-09-25, MICHAEL-NETBOOK, boot 10, shared ABI 2026092503. 3DMark 99
Max at 1024x576x16, triple buffering: 244 3DMarks (245 and 246 before; the
instrument costs nothing measurable). Report:
`2026-09-25-netbook-batch-shape-3dmark1024-report.txt`, from
`...-batch-shape-before-V9XTRACE.ini` and `...-3dmark1024-V9XTRACE.ini`.

## The question

`2026-09-25-where-the-hal-time-goes-on-the-netbook.md` found 43% of the
run spent waiting for RING_HEAD, 877 us a batch, and could not say whether
that was a fixed cost per submission (the flushes and state reload every
batch carries) or the work in it. This run files every draw batch's head
wait by its triangle count and by the screen area its triangles cover
(summed from the vertices, so overdraw and depth-rejected pixels count).

## Head wait per batch, microseconds (batches)

| triangles | <1K px | 1K-10K | 10K-50K | 50K-200K | 200K-1M | 1M+ |
|---|---|---|---|---|---|---|
| 1 | 5 (9,882) | 248 (150) | 1,934 (17) | - | - | - |
| 2-3 | 54 (23,408) | 412 (12,765) | 2,219 (1,944) | 8,214 (104) | - | - |
| 4-7 | 32 (3,542) | 413 (1,154) | 2,435 (424) | 10,233 (188) | 67,185 (130) | - |
| 8-15 | 71 (3,915) | 405 (806) | 2,293 (340) | 12,572 (244) | 22,796 (168) | 129,669 (64) |
| 16-31 | 64 (3,377) | 484 (2,025) | 2,740 (234) | 11,400 (74) | 67,445 (1,514) | - |
| 32+ | 59 (92,821) | 311 (42,563) | 2,513 (4,935) | 8,556 (576) | 27,813 (96) | - |

## What it says

- **The fixed cost per batch is about 5 us**, the one-triangle, sub-1K
  cell over 9,882 batches. The leading MI_FLUSH_READ, the state block and
  the trailing MI_FLUSH are therefore not where the time goes; trimming
  them is not worth doing for speed.
- **Triangle count barely matters**: down any column the cost moves by a
  factor of two or less while the triangle count moves by thirty.
- **Area is what costs**: every row rises by roughly an order of magnitude
  per area class, at about 70-130 ns a pixel - **about 10 Mpixels/s**,
  which is 3DMark's own 8.1 MTexels/s fill rate at this resolution. That is
  a very small fraction of what this part should fill; the pixel work
  itself is slow.
- So asynchronous submission can hide at most the head wait behind the
  application's own work; it cannot make the GPU draw faster, and at this
  fill rate the GPU is the limit whenever the scene is large.

## A lead, not a finding

3DMark's fill rate was 38.8 MTexels/s at 640x480 against 8.1 at 1024x576
(`2026-09-25-netbook-3dmark99-with-mip-trees.md`). A pixel pipeline with a
fixed cost per pixel would fill at the same rate at both. What changes
with the mode is the layout: the colour and depth surfaces' pitch goes
from 1280 to 2048 bytes, a power of two, which can put every row of both
surfaces on the same DRAM banks. That is a hypothesis about memory
behaviour nobody has measured. The same table at 640x480 is the next
measurement: a per-pixel cost that differs by resolution points at layout,
the same cost at both points at the pipeline.

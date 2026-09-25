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

## Measured at 640x480: the per-pixel cost depends on the mode

Same boot, same build, 3DMark 99 at 640x480x16, triple buffering: 692
3DMarks. Report `2026-09-25-netbook-batch-shape-3dmark640-report.txt`.

| triangles | <1K px | 1K-10K | 10K-50K | 50K-200K | 200K-1M |
|---|---|---|---|---|---|
| 1 | 7 (11,979) | 82 (231) | 168 (14) | - | - |
| 2-3 | 27 (46,158) | 161 (13,289) | 211 (852) | 611 (40) | - |
| 4-7 | 10 (9,206) | 58 (2,823) | 241 (854) | 580 (326) | 2,850 (1,016) |
| 8-15 | 12 (5,438) | 68 (2,117) | 320 (597) | 1,018 (1,301) | 5,697 (508) |
| 16-31 | 12 (3,990) | 77 (1,837) | 270 (2,262) | 861 (96) | 3,029 (1,506) |
| 32+ | 21 (85,699) | 45 (66,645) | 195 (7,766) | 995 (1,148) | 2,264 (12) |

Every area class costs about **ten times less per batch than at
1024x576** - around 10 ns a pixel, 100 Mpixels/s, against 70-130 ns. The
head wait falls from 43% of the run to 2.7% (91.5 us a batch against
877), and the fixed cost is the same few microseconds. So the slow fill
at 1024x576 is not the pixel pipeline's own rate: something about that
mode makes each pixel about ten times dearer.

What differs between the two modes, none of it yet tested:

- the colour and depth pitch, 2048 bytes (a power of two) against 1280;
- where the three colour buffers and the Z buffer sit, 1.18 MB each
  against 0.61 MB, and so how they alias each other in the render cache
  and in DRAM banks;
- the amount of heap left for textures (the mip-tree declines show it is
  short at 1024x576), which changes nothing on the GPU's pixel path but is
  recorded because it differs.

The discriminating experiment is the 1024x576 mode with a non-power-of-two
pitch for the surfaces the GPU renders into (the Z buffer at least, whose
pitch the driver controls independently of the scanout), or with the
buffers moved so the colour and Z rows do not alias.

## The lead that prompted it

3DMark's fill rate was 38.8 MTexels/s at 640x480 against 8.1 at 1024x576
(`2026-09-25-netbook-3dmark99-with-mip-trees.md`). A pixel pipeline with a
fixed cost per pixel would fill at the same rate at both. What changes
with the mode is the layout: the colour and depth surfaces' pitch goes
from 1280 to 2048 bytes, a power of two, which can put every row of both
surfaces on the same DRAM banks. That is a hypothesis about memory
behaviour nobody has measured. The same table at 640x480 is the next
measurement: a per-pixel cost that differs by resolution points at layout,
the same cost at both points at the pipeline.

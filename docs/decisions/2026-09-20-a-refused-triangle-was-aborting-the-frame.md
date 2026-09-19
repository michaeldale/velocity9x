# A refused triangle was aborting the frame

2026-09-20, 86Box, two guests side by side: `Win86SE` (Velocity9x, ViRGE/DX,
port 9869) and `Win98SE-Native-S3` (S3's own driver, same chip, port 9870).
Final Reality 1.01, Direct3D On-board Accelerator on both.

## The comparison found in one screenshot what four runs had missed

Native S3 rendered the benchmark. Velocity9x put up:

```
DrawPrimitive DDERR_INVALIDPARAMS
```

and quit, on a black screen. Four instrumented 3DMark99 runs had reported
zero refusals and six million triangles; none of them could show this,
because an application aborting is not a refusal and no counter was watching
for it.

## Two defects, and a wrong first diagnosis

`0x80070057` is set in three places in `d3d_core.c`, and it was **assumed** to
be the newly written indexed path. That was recorded here as "a regression,
and it is mine" before it was established. Correcting the two
single-primitive paths did not stop the error, which is how the assumption
was caught.

**The first defect is real and was mine.** `V9xD3dDrawOneIndexedPrimitive`
and `V9xD3dDrawOnePrimitive` answered `DRIVER_HANDLED` with an error for a
shape they could not serve. The stub that preceded them returned
`DRIVER_NOTHANDLED`, which asks the runtime to do the work. Saying *handled,
and it failed* where the truth is *I cannot* tells an application its
parameters are wrong. They now decline; a supported shape whose draw
genuinely fails still reports the error, because that one is the driver's
fault.

**The second is older and is what actually killed the benchmark.**
`v9x_d3d_virge_draw_triangles` failed the entire batch on the first triangle
`v9x_d3d_triangle` declined:

```c
for (index = 0ul; index < triangle_count; ++index) {
    if (!v9x_d3d_triangle(context, &vertices[index * 3ul])) {
        return 0;
    }
}
```

That predates all of this week's work. It never mattered while the indexed
path was a stub, because nothing reached it; implementing the path routed
Final Reality straight into it. The counters say the shape of it exactly:
64 triangles drawn - one full batch - then a decline, then the error.

The rest of this file had already settled the principle. A texture the unit
cannot sample draws untextured rather than not at all, because a refused
draw is a hole in the frame. A refused triangle is a smaller hole; an
aborted batch is a much larger one. So declines are counted and skipped, and
the call fails only when nothing at all could be drawn.

## Measured after the change

```
IndexedCalls=1152   IndexedDrawn=1152   IndexedTriangles=147456
IndexedRefusedShape=0   IndexedRefusedIndex=0
TrianglesDeclined=2304
DpPrimTypeSeen=0x00000040   DpRefusedPrimType=1   DpRefusedVerticesLast=3
```

Every indexed call served, and Final Reality renders instead of quitting.
2,304 triangles declined out of 149,760 - **1.5 per cent** - which is the
size a stitched strip's degenerate triangles would be, and small enough that
skipping them is plainly better than aborting.

`DpPrimTypeSeen=0x40` is bit 6, `D3DPT_TRIANGLEFAN`: the application sends
one fan through `DrawPrimitives` and it is refused. One occurrence of three
vertices, so it is not what the pictures show, but it is the next primitive
type this driver does not serve.

## What is NOT established

Whether the picture is correct. Both guests render the intro, and at sixty
seconds in they are at different points of it because Velocity9x is slower,
so the two frames cannot be compared. The Velocity9x frame is saturated -
pure blue, green and magenta with a white core - where the native is a
blended purple and orange. That may be a different moment of an animating
scene or it may be a blend defect, and one unaligned pair of screenshots
cannot tell them apart.

What is established is that the benchmark completes a run instead of
aborting on its first refused triangle.

## The lesson worth keeping

Four instrumented runs of 3DMark99 reported success while the application
this project exists to fix would not start. The counters were not wrong -
they measured what they were built to measure - and 3DMark99 never exercised
the path that failed. A second machine running a different driver found it
immediately.

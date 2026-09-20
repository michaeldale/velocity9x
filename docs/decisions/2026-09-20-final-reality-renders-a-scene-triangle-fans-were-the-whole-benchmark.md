# Final Reality renders a scene: triangle fans were the whole benchmark

2026-09-20, 86Box `Win86SE` (Velocity9x, ViRGE/DX) against
`Win98SE-Native-S3` (S3's own driver, same chip). Final Reality 1.01,
Direct3D On-board Accelerator. Attached:
`2026-09-20-fr-v9x-fans.png`, `2026-09-20-fr-v9x-fans-V9XSNA11.txt`.

## Three failures in a row, each hiding the next

The side-by-side run exposed them one at a time, and each had to be fixed
before the next became visible.

**One: an error where the truth was "I cannot".** The two single-primitive
paths answered `DRIVER_HANDLED` with `DDERR_INVALIDPARAMS` for shapes they
did not serve. Fixed to decline.

**Two: one refused triangle aborting a batch.** The ViRGE's per-triangle
loop returned on the first decline. Fixed to skip and count.

**Three: an engine refusal reported as a parameter error.** Every remaining
draw path - `RenderPrimitive`, `DrawPrimitives`, both single-primitive
entries - turned a refusal this driver owns into a description of the
application's call. They now answer `DD_OK` and count
`batches_engine_refused`, because a hole in the frame is what this driver
already chooses over an abort and an uncounted hole is what it does not.

With those three done the benchmark stopped quitting - **and drew a black
screen.**

## And then the measurement that explained the black screen

```
D3dRenderPrimitiveCalls=192814
DpPrimTypeSeen=0x00000040        (bit 6: D3DPT_TRIANGLEFAN)
DpRefusedPrimType=192259
BatchesEngineRefused=192259
```

**192,259 records refused, every one a triangle fan.** That is every test
Final Reality runs past its intro. The driver was not drawing a black screen
because anything was broken downstream; it was refusing the only primitive
type the benchmark uses.

The fan was worth nothing to implement and everything to know about, and it
took the two-VM comparison plus three prior fixes to become visible at all.

## Fans, drawn

A fan of N vertices is N-2 triangles sharing vertex 0, so unlike a list it
is gathered rather than pointed at, in the same bounded batches the indexed
path uses. After the change:

```
DpRefusedPrimType=0   DpRefusedVertType=0   DpRefusedCount=0
D3dRenderPrimitiveCalls=79613
```

Zero refusals, and **Final Reality renders a recognisable scene** - walls, a
road, structures, correct perspective and depth. The screenshot is attached.

## What is wrong with it

The picture is heavily speckled with green. Geometry and depth look right;
colour does not. Two counters bear on it and neither explains it:

```
TrianglesDeclined=58726     BatchesEngineRefused=34166
FilterMagSeen=0x0000000E    D3dTextureRefusedFormat=0
```

58,726 declined triangles and 34,166 refused batches are both far larger
than the 2,304 and 0 of the intro-only run, so a real fraction of the scene
is missing and that alone could produce speckle. `FilterMagSeen` is now
`0x0E` - NEAREST, LINEAR and MIPNEAREST - so more filter states arrive than
before.

No texture was refused for format or shape, so the textures are being
accepted. Whether the green is missing geometry, a texel format error, a
blend error or the colour-key path is **not established**, and the previous
guess in this investigation about a picture defect was wrong.

## Standing

Final Reality goes from "quits with an error box" to "renders a scene with
wrong colour". That is a large step and it is not a finish. The native S3
guest renders the same scene correctly on the same chip and is the
comparison to keep using.

# Final Reality renders green speckle on the ViRGE

**Status: OPEN, parked 2026-09-20** to work on 3DMark99 on the Intel
netbook. Nothing here blocks that.

## What it looks like

After the draw-path work of 2026-09-20, Final Reality 1.01 on the 86Box
`Win86SE` guest (ViRGE/DX, Direct3D On-board Accelerator) renders a
recognisable scene - walls, a road, structures, correct perspective and
depth - heavily speckled with green.
`docs\decisions\2026-09-20-fr-v9x-fans.png` is the frame. The
`Win98SE-Native-S3` guest renders the same scene correctly on the same
emulated chip.

## Is it worse than before?

**Not established, and worth stating plainly rather than assuming.**

The before-state was never measured on this guest. What the code says is
that `V9xD3dDrawPrimitives` set `ddrval` to `DDERR_INVALIDPARAMS` on a
triangle-fan record long before this week, and that every Final Reality test
past the intro is fans - so the error box was almost certainly there
already, and the change is from aborting to rendering-with-wrong-colour.

That is reasoning from the source, not a measurement. Checking out `32b558c`
and running Final Reality once would settle it and has not been done.

## What is measured

```
DpRefusedPrimType=0          (fans accepted; was 192,259 refusals)
TrianglesDeclined=58726
BatchesEngineRefused=34166
FilterMagSeen=0x0000000E     (NEAREST, LINEAR, MIPNEAREST)
D3dTextureRefusedFormat=0    D3dTextureRefusedShape=0
```

58,726 declined triangles and 34,166 refused batches are far above the 2,304
and 0 of the intro-only run, so a real fraction of the scene is not being
drawn, and missing geometry alone could produce speckle. No texture was
refused for format or shape, so textures are being accepted.

## Candidates, none established

- **Missing geometry.** The two counters above are large. This is the one
  with numbers behind it.
- **A texel format error.** Textures are accepted; whether they are read as
  the format they were created in is untested.
- **A blend or colour-key error.** More filter and blend states reach the
  driver than ever before, and several of them are newly arriving.
- **The engine's own colour path.** Untested against the native driver.

The last guess made about a picture defect in this investigation was wrong.
These stay a list until one of them has a number.

## Where to start

`v9x_d3d_triangle` declines for roughly a dozen distinct reasons and
`triangles_declined` lumps them together. A per-reason split is the same
instrument-first step that turned the fan question from a guess into one
number, and 58,726 is a large enough population to be worth naming.

The two-VM comparison is the gate to keep: `Win98SE-Native-S3` on port 9870
renders this correctly and is the reference.

## Risk introduced on 2026-09-20 and carried knowingly

A batch the engine refuses now reports `DD_OK` and increments
`batches_engine_refused`, rather than returning `DDERR_INVALIDPARAMS`. This
stops applications aborting and it makes missing geometry **silent**. The
counter is the only thing standing between that and an unexplained hole, so
a large `BatchesEngineRefused` must be treated as a defect and not as
background.

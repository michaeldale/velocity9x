# The DX5 draw paths never clipped, and the S3D drops what reaches the edge

2026-09-23, A8U4I5 (S3 Trio3D/2X `5333:8A13`, Windows 98 SE), boots 103-105,
3DMark 99 Max at 640x480x16, triple buffer, `Direct3DMode=hardware`. Driver
built from `3692595`, then the same tree with this change.

## What was seen

A full 3DMark 99 run on boot 104 (526 3DMarks) was captured frame by frame
and set against reference runs of the same tests on other hardware.

- **Fill Rate** drew nothing. Three consecutive frames of the single-texture
  test are entirely black (`../images/3dmark99-trio3d-2026-09-23-fillrate-before-clip.png`).
  The reference is four full-screen planes of "3DMARK" lettering.
- **Texture Filtering**, a four-walled checkerboard tunnel in the reference,
  drew one or two walls against black
  (`../images/3dmark99-trio3d-2026-09-23-tunnel-before-clip.png`).
- The run's snapshot: `TrianglesDeclined=66990`, `BatchesEngineRefused=4012`,
  every texture refusal counter zero, no engine fault
  (`2026-09-23-trio3d-3dmark99-before-clip-V9XSNA2.ini`).

## Cause

`src/display32/d3d/d3d_internal.h` documents that an engine receives
vertices "already colour-adjusted and clipped by the core". Only
`V9xD3dRenderPrimitive`, the execute-buffer path, clipped. `V9xD3dDrawOnePrimitive`,
`V9xD3dDrawPrimitives` and `V9xD3dDrawOneIndexedPrimitive` handed the
application's vertices straight to `v9x_d3d_draw_batch`. 3DMark 99 uses those
three and nothing else (`CountD3dDrawPrimitives`, `CountD3dDrawOneIndexed`;
no RenderPrimitive in the snapshot).

The S3D emitter, `v9x_d3d_triangle`, returns 0 for any triangle with a vertex
outside `[0, width-1] x [0, height-1]`, and that return is what
`TrianglesDeclined` counts. A full-screen quad is 0..640 x 0..480 in
Direct3D's convention, so every fill-rate plane was declined whole, and so
was every tunnel wall that ran off the screen.

## Change

- The three DX5 paths draw through `v9x_d3d_draw_list`, which passes
  triangles already on the target through as runs and cuts only those that
  cross an edge, with the existing clipper.
- The clipper's right and bottom edges are `width` and `height`, not
  `width - 1` and `height - 1`, and the S3D emitter accepts a vertex on them.
  Cut at 639, a span ends one column early on an engine that fills
  `[ceil(x1), ceil(x2))`. The pixel past the edge is discarded by the S3D's
  clip rectangle: every command word carries `cmdHWCLIP_EN` (bit 1 of the
  `0x7` low bits, `98DDK s3v\VIRGE1.H:129`) with `CLIP_L_R`/`CLIP_T_B` at
  `width - 1`/`height - 1`, which is also what S3's driver does
  (`s3v\D3DRENDR.C:64, 399-427`).
- `V9X_D3D_ENGINE_LIMITS` gains `clip_in_core`, appended: set for the S3D and
  software engines, clear for Gen3. The Gen3 path ran 3DMark 99 unclipped on
  the netbook within its 4096 guard band and is not re-measured here, so its
  behaviour is kept.

## Measured after

Boot 105, same machine, 3DMark 99 with only Fill Rate, Texture Rendering and
Texture Filtering selected (`2026-09-23-trio3d-3dmark99-after-clip-V9XSNA3.ini`):

| | before (full run, b104) | after (three tests, b105) |
|---|---|---|
| `TrianglesDeclined` | 66,990 | 0 |
| `BatchesEngineRefused` | 4,012 | 0 |
| Engine FIFO/idle timeouts, resets | 0 | 0 |
| Texture refusals, blend skips | 0 | 0 |

The two runs cover different tests; the claim is only that the declines went
to zero, not a per-test comparison.

- Fill Rate draws its layered lettering
  (`../images/3dmark99-trio3d-2026-09-23-fillrate-after-clip.png`), dimmer
  than the reference. The card reports no additive blending, which is what
  makes the reference white.
- The tunnel fills the screen with four walls
  (`../images/3dmark99-trio3d-2026-09-23-tunnel-after-clip.png`). **The
  checkerboard is still absent in every filter mode**, point sampling
  included. The walls draw with `0x94E0x047` - UNLIT, 256-texel ARGB1555,
  mipmapped, chains without gaps - so the gradient is in the texture and the
  checker is averaged away: the per-triangle mip level, taken from the
  triangle's largest affine derivative, is too coarse over a wall running from
  the screen edge to the vanishing point. Not fixed here.

## What this does not settle

- **Texture Rendering.** Its tiles draw with `0x8C68E027` - ARGB4444, 128
  texels, MODULATE, bilinear, texel alpha blending. That is the draw the
  Trio3D's bad blend state breaks, and every run today was in it
  (`TexMatrixOk=90` of 117 on boots 103 and 104). Whether anything else is
  wrong with those tiles cannot be read until the card is in the good state.
- **Race and corridor.** Both lost geometry to the same decline and should
  be re-read; they were not run after the change.
- **The Gen3 engine** is unchanged by construction and was not run.

## Also measured: the blend-state marker is dead

`docs/decisions/2026-09-05-a-register-capture-of-both-trio3d-blend-states.md`
left Input Status 0 bit 4 as the one register that had tracked the state,
four boots for four. Boot 104 was in the bad state (`TexMatrixOk=90`, every
alpha rung 0) and read `InputStatus0=09`, the value both good boots read
(`2026-09-23-trio3d-a8u4i5-vgasurv-b104-bad.ini`, survey `/notier2`, run
detached). Nothing the survey reaches tracks the state. A warm restart between
boots 103 and 104 left it bad, as on earlier boots.

## Gates

`./scripts/check-tree.ps1` (its Gen3 `texture_align` pattern now allows the
comma the appended field needed), `./scripts/build-host.ps1` and
`./scripts/run-checks.ps1` passed. The change is untested on the software
rasterizer and on the emulated ViRGE.

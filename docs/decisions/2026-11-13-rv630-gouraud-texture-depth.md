# HD 2600 XT: Gouraud and textured draws exact; depth writes one constant Z

Date: 2026-11-13

Status: **measured, one card, boot 5 (about 70 runs) and boot 6 (depth recheck).** M-NZHWBR's HD
2600 XT (`1002:9588`, RV630), the `ati` driver of `2c6eeccb` running
DirectDraw on DMA throughout. The 3D step of
[the plan](../plans/ati-rv630-hd2600xt.md), after the first solid draw
([record](2026-11-13-rv630-first-3d-draw.md)). Tool: `V9XR63D.EXE`
(`/gouraud`, `/depth`, `/modulate` and their diagnostic switches) on
`EGCP.VXD`, with `r6_3d.c` / `r6_shader.c` grown into a batch-state
builder (`struct v9x_r6_3d_state`, shaped as Evergreen's for a later
`d3d_r6.c`), host-tested by `test_r6_3d.c`. Captures, one INI per run,
lettered by set a-q: [`../probe/rv630-3d-2026-11-13/`](../probe/rv630-3d-2026-11-13/).

## Gouraud: exact

Two triangles over the 64x8 target, red rising 0 to 1 left to right,
green 0.5, blue 0.25: all 512 pixels within +-2 on every run, guards
intact, at the default vertex address and at `/vboff:C000`.

## Modulate: exact once every vertex fetch was a mega-fetch

The texture sampled point-wise at texel centres times the interpolated
colour, Direct3D's default stage. Wrong in three ways before it was
right; the measurements that settled it:

| Set | Change | Result |
|---|---|---|
| a | first build | right texels, colour multiplier wrong in every channel |
| b | fetch order, PS writing a third GPR | same; `/white` and `/twocol` (no texture) wrong identically: the colour interpolant itself |
| c-f | MEGA_FETCH cleared on all fetches ("Mesa's form", misread) | G and B exactly halved, R and A right, at a fresh vertex address and a new texture seed: not stale caches |
| g | MEGA_FETCH set on every fetch, count 1Fh (Mesa's r600_bytecode_vtx_build before Cayman) | **all three variants exact**, 0 mismatches |

Rule, now a host check: the first fetch of every vertex clause is a
mega-fetch. Gouraud's shader always started with one, which is why it
was exact from the first run.

## Depth: open

A Z16 buffer, 1D tiled (R6xx has no linear depth, Mesa), LESS with writes:
nothing is drawn. What the runs settled:

- The readback is sound (`/nodb /zfill:30003000` reads back 3000).
- Depth writes are real and per fragment: with only the left rectangle
  drawn, tiles 0-3 change and 4-7 keep the fill (`/zleft`, `/zright`).
- Every fragment's Z is one constant: in DEPTH_32_FLOAT (`/set:28010=
  10006`) every written value is `4994E8C4` (1,219,864.5); Z16 saturates
  it to FFFFh, which first looked like "Z = 1.0".
- It does not move with: the vertices' z (`/zall`), w (`/wv`), a z the VS
  writes from a constant before the position export (`/zlit`, `/posw`),
  screen position, the viewport Z scale/offset in window-space and in
  Mesa's ordinary form (`/zscale`, `/zoffset`, `/mesavte`), the Z clamp
  (`/zmin`, `/zmax`), DB_DEPTH_CLEAR (`/zclear`, in Z16 and Z32F),
  DB_RENDER_CONTROL and DB_RENDER_OVERRIDE (0, xf86's, Mesa's), Z order
  and dual export (DB_SHADER_CONTROL), the interpolator (SPI_PS_IN_
  CONTROL_0 with POSITION_ENA, SPI_INPUT_Z), a Z exported by the PS
  (`/zexport`), Mesa's ELEM_SIZE on the exports (`/elem3`), every
  context register Mesa programs differently (`/mesadb`), all of Mesa's
  path at once with clipping on (`/mesaall`), the depth array mode, and
  DB_HTILE_DATA_BASE pointed at zeroed memory with DB_HTILE_SURFACE 0.
- No register holds it: all 1,024 context registers read back after the
  draw by MM_INDEX/MM_DATA (`/ctxdump`) hold what the stream wrote
  (CB_SHADER_CONTROL, R7xx-only, the one exception), viewport 0 is
  (1, 0, 1, 0, 1, 0, 0, 1); viewports 1-15 hold junk, none the constant.
  The PA/SC/SX/SPI/DB config registers read directly (`/cfgdump`) hold
  none of it either.

So the DB takes its Z from neither the rasteriser, nor the PS, nor any
register we can read. Filed as
[an issue](../issues/2026-11-13-rv630-depth-constant-z.md).

## Not measured

- What the rest of config space, or memory-controller and ARB state the
  BIOS left (Linux's r600_startup writes HDP/DCP/DMA tiling copies and
  ARB_POP, which this probe skips), do to the DB.
- Depth on any other R6xx board.

# DDI 8: the MIPFILTER stage state is read with DirectX 7's numbering, so DirectX 8 programs lose mipmapping (speckled distant textures)

Date: 2026-11-19. Status: fix written and host-tested; not yet run on
hardware. Seen on the HD 4600 (i5-4570) in 3DMark 2001 SE.

## What was seen

Michael's OBS capture `2026-11-19 19-54-02` (Car Chase, game time 11:28):
speckling on the road and grass in motion, "almost seems a filter or
something is missing". The same grain is in the 16:40 capture at game time
11:20, before the texture pool, so it is older than the pool.

## The evidence

From the full 3DMark 2001 SE run of the same day
(`docs/probe/hd4600-texture-pool-phase4-2026-11-19/full3dm/`):

- Mip chains were placed and verified: `MipLevelsMax=11`,
  `D3dMipChainLevels=8`, no chain gap.
- `FilterMinSeen=0x00000006`: the min filter the core folds from MINFILTER
  and MIPFILTER (`v9x_d3d_min_filter_fold`, `d3d_core.c`) was only ever
  NEAREST (1) or LINEAR (2), never one of the MIP filters (3-6). Haswell
  samples a texture whose min filter has no MIP part at level 0 only
  (`v9x_d3d_hsw_mip_filter`), so distant surfaces read their largest
  level: shimmer and speckle.

## The cause

DirectX 7's `D3DTEXTUREMIPFILTER` is NONE 1, POINT 2, LINEAR 3; DirectX 8's
`D3DTEXTUREFILTERTYPE` is NONE 0, POINT 1, LINEAR 2. A DDI 8 driver is
handed DirectX 8's. The fold compares against DirectX 7's: a DirectX 8
POINT mip filter read as NONE (no mipmaps), and LINEAR as POINT (nearest
level, no blend between levels). The pixel-shader stage
(`V9X_D3D_PS_STAGE`) keeps DirectX 8 values by design (its default
`mip_filter` is 0) and fed them to the same fold.

Not measured: which value 3DMark sends. The counter records the folded
result, not the raw state.

Affected: every engine that offers DDI 8 (`V9X_DD_ENGINE_CAP_D3D_DDI8`):
Haswell, the Oland, the GT218 and the GMA 950's DDI 8 path. MINFILTER and
MAGFILTER are not affected: their POINT and LINEAR are 1 and 2 in both.

## The fix

`v9x_d3d_ps_mip_filter_dx7` (`d3d_ps.c`, pure): DirectX 8's value to
DirectX 7's (0->1, 1->2, 2 and above->3). Used for stage 0's and stage 1's
MIPFILTER on a DDI 8 context (`context->ddi8`) and for the pixel-shader
stage before its fold. `mip_filter_tests` in `test_d3d_ps.c` was written
first and failed six checks against a stub. `run-checks` passes.

## To confirm on hardware

3DMark 2001 SE on the HD 4600: `FilterMinSeen` should gain a MIP bit
(3-6), and the road and grass should stop sparkling in an OBS capture.

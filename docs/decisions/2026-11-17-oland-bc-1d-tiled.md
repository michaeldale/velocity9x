# Oland: DXT as BC1-3 in 1D-thin tiles

Date: 2026-11-17

Status: measured on W9QI0DAD (Oland XT `1002:6610`, FX-6300, Win98 SE).
The layout probes ran at boot 61 with V9XSICP, outside the HAL. The HAL
build ran at boot 63: built from this commit's tree (`a369e309-dirty`),
its DRV, VXD, HAL and SETP put in place by WININIT rename and
hash-checked. This is step 7 of
[ati-gcn-ps11.md](../plans/ati-gcn-ps11.md), on the Oland alone. It
follows [the linear-aligned probe](2026-11-17-oland-bc-linear-probe.md),
whose rule was dead.

Evidence: [`../probe/oland-dxt-w9qi0dad-2026-11-17/`](../probe/oland-dxt-w9qi0dad-2026-11-17/).

## Finding the layout

V9XSICP `/ps11:9` (BcMap) draws a BC1 texture over 10 KB of memory.
- Every halfword h holds FFFFh − h, so any two adjacent halfwords make a
  4-colour pair whose colour 0 names its own halfword.
- Each drawn pixel then names the halfword its colours came from, to
  within one, whatever its indices were.
- Fitting each texel coordinate bit against the change of address it
  causes, over the `[BcMap] Ryy` pixel dumps, gives the layout. The
  fitting script is not in the tree.

What the runs showed, in order:
- **Tile index 8 (linear aligned), as radv writes the T# for GFX6.**
  Only the first texel row of each block read inside the map. The block
  row stride grew with the width and the pitch (512, 1024 and 2048 bytes
  for widths 8, 16 and 32 at pitch 32; 2048 at width 16 and pitch 128).
  It is not a layout this driver can use: `sicp-bcfit2`.
- **WIDTH, HEIGHT and PITCH in blocks** (`/bcblk`): Bc1 still had 1,405
  of 1,572 pixels wrong.
- **Tile index 26 (LINEAR_GENERAL)**: 1,526 wrong.
- **Tile indices 9 and 13 (1D tiled thin)** with the old linear data:
  536 wrong. Under these the map fits cleanly (`sicp-bcti`):
  - the texel bits within a block move nothing;
  - index 13 (THIN micro tiling), per 8-byte BC1 block: x0 8, y0 16,
    x1 32, y1 64, x2 128, y2 256. That is Morton order in an 8x8-block
    tile of 512 bytes;
  - index 9 (DISPLAY micro tiling): x0 8, y0 16, x1 32, x2 64, y1 128,
    y2 256;
  - between tiles, under index 13: the next tile across is +512, the
    next tile row +1024 at a 64-texel pitch and +2048 at 128 (pitch / 32
    tiles to a row). Measured with 64x64 and 128x32 textures.

Mip levels (`/bclN`: BASE_LEVEL = LAST_LEVEL = N, `sicp-bc-mips`): in a
64x64 BC1 chain the levels start at 0, 2048, 2560, 3072, 3584, (4096),
4608. Each level's blocks are rounded up to whole tiles, so a level
smaller than a tile still takes one. Each level reads with the same
in-tile order. The chain measured was square; non-square ones are not.

The check (`sicp-bc-1d`): `/ps11:6-8` laid out by the rule under tile
index 13, each block's own exact colours and alpha.
- Bc1, Bc2 and Bc3 at 32 and 16 bpp: 1,572 pixels judged in each, 0
  bad, worst step 0.
- BC decodes red into x: with the T#'s identity swizzle the pixel shader
  reads red in r.

## The change

- **`vi_3d.c`**: `v9x_vi_3d_bc_block_offset` (the tile rule) and
  `v9x_vi_3d_bc_layout` (a chain's levels), host-tested against the
  measured offsets. The tests were written first and failed against
  stubs.
  - The T# takes BC1-3 (35-37).
  - A shader stage reads it unswizzled; the fixed-function path swaps x
    and z for its blue-in-x order.
  - `tl_draw` accepts the formats.
- **`d3d_vi_map.c`**:
  - a BC texture maps with tile index 13 (`V9X_VI_TILE_BC`);
  - its pitch must be whole tiles (32 texels);
  - a colour key on one is refused, since a block has no texel value to
    compare.
- **`d3d_vi.c`**:
  - DXT1/3/5 FourCCs are BC1-3.
  - `create_surface` places a DXT level, chain or cube in tiles. lPitch
    is the level's blocks across times the block's bytes.
  - `resolve_texture` refuses a DXT surface DirectDraw placed.
  - A DXT cube puts each face in tiles, level-major as the linear cubes.
    That combination is unmeasured.
- **Lock hook** (as the HD 4600's): Lock points an engine-placed DXT
  level at a packed linear copy filled from the tiles; Unlock writes it
  back block by block. A level destroyed while locked is pointed back
  first.
- **`dxt_textures`** is set in the Oland's limits, so DDI 8 publishes
  DXT1, DXT3 and DXT5.

## Measured, boot 63

V9XD32:
- 32 bpp desktop (X8R8G8B8, tolerance 2): T19 Dxt1 and Dxt3, 1,040
  pixels each, 0 mismatches.
- 16 bpp desktop (R5G6B5): the same.
- T12-T16 pass, Cube and Normal pass, and FfSpecFog's known mismatch
  remains, in both runs.
- `EngineIdleTimeouts` stayed 0.

3DMark 2001 SE, default benchmark (`3dmark2001se-boot63`). It ran with
"DXTC" in its settings line. Michael closed it in Nature, which was too
slow to go on. His OBS recordings are on the host, not in the tree:
`Y:\MWD\videos\obs\2026-11-17 16-36-03.mkv` and `16-39-33.mkv`.

- Dragothic Low drew at about 60 fps.
- Dragothic High ran at 0-2 fps, with whole black frames, black
  buildings and dragon against a white sky, white rectangles and green
  garbage.
- Nature drew for the first time on the Oland: sky, mountains, trees and
  flowers look right.
  - The water is a flat grey-blue in one frame and banded grey speckle
    in another.
  - The cliff's lower edge is smeared.
- After the run:
  - **the CP had stalled** (`EngineIdleTimeouts` 0 then 1). That is the
    third stall in four 3DMark 2001 SE runs on DDI 8 builds
    ([issue](../issues/2026-11-17-oland-3dmark2001se-cp-stall-hang.md));
  - **23,760 draws refused for texture format** (reason 9);
  - **130,775 texture locks**, against 64 before the run.
- `V9XPS.LOG` filled its 6,000 lines for the boot with surface
  creations:
  - 1,918 DXT1, two DXT3 and two Q8W8V8U8;
  - about half of all creations were declined (result 0), most of them
    chains of 9-11 levels;
  - 16,271 placements and 3 GB placed over the run.
- **The heap is 16 MiB.** `VbeVramBytes=16777216`, the VBE's capped
  figure (the [first survey](2026-11-16-oland-first-survey.md)). After a
  1024x768x32 front and back buffer about 10 MB is left for textures.
  - Dragothic High's and Nature's DXT sets do not fit. The runtime evicts
    and re-uploads textures all the time, and each upload's lock now
    copies every block between the tiles and a linear copy.
  - Uploads that find no room are placed by DirectDraw, which this build
    then refuses as untiled (reason 9).
  - That accounts for the missing textures and most of the slowness.
- Not separated from the above: the CP stall's black frames, and
  whether Nature's water is wrong in its own right.

## Not done or not known

- **The heap.** The Evergreen rows map 128 MiB of BAR0 for theirs
  (`cedar_hw16.c`, after Quake 3 and 3DMark 2000 ran out of room in
  16 MiB). The Oland still has the family's 16 MiB. That is the next
  change, and 3DMark 2001 SE's texture tests (this step's gate) wait for
  it.

- Non-square DXT chains, and DXT cubes, are unmeasured.
- DXT5 is published and no judged draw reads it (T19 has Dxt1 and Dxt3).
- Why linear-aligned BC samples as it does.
- W9QI0DAD froze just after the desktop on this build's first boot,
  before any program of ours ran. It wrote no fault record, and the next
  boot was fine. Not known whether this build caused it.

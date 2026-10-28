# Carrizo's fixed function in the pixel shader: blending needed an FP16 export, a 565 texture needs W = 1, mips measured

Date: 2026-10-28

Status: **measured, one machine.** M-XP7053's Radeon R7E (`1002:9874`),
boots 22 to 28. The builds were uncommitted on a63f5b1c. This is Phase 5
steps 2 and 3 of [the 3D plan](../plans/ati-carrizo-gfx8-3d.md). The
captures are in
[`../probe/m-xp7053-carrizo-d3d-fixed-function-2026-10-28/`](../probe/m-xp7053-carrizo-d3d-fixed-function-2026-10-28/).

## What was built

- **Blending.** `CB_BLEND0_CONTROL` comes from `v9x_vi_3d_blend`, with
  colour and alpha the same and ENABLE set. `BLEND_CLAMP` is set in
  `CB_COLOR0_INFO` while blending. Every factor except destination alpha
  is offered; Direct3D's destination-alpha factors are refused.
- **A pixel shader built per draw** (`v9x_vi_shader_d3d_ps_built`) for
  whatever the fixed PS slots do not cover. In order it applies:
  1. the colour key on the texel: the sum of squared differences against
     the key, and `v_cmpx_lt_f32` against Cedar's tolerance squared;
  2. the texture op;
  3. specular;
  4. fog, a per-channel mix toward the fog colour by the factor the VS
     passes in the specular's w (Direct3D's specular alpha / 255);
  5. the alpha test, a `v_cmpx_<func>` of the final alpha against a
     literal reference.

  Each kill branches with `s_cbranch_execz` to an `exp null done vm`
  tail, and the draw sets `DB_SHADER_CONTROL` KILL_ENABLE. Its constants
  are in its code, and it goes into the vertex arena with each batch, as
  Cedar's lit PS does. The VOPC and VOP2 encodings are pinned against
  LLVM's GFX8 disassembler tests in `test_vi_3d.c`.
- **ARGB1555 and ARGB4444 textures** (`IMG_DATA_FORMAT` 11h and 13h),
  which read in memory order like the other two formats.
- **A partial Z16 clear** (`v9x_d3d_vi_depth_fill`). It is a CP draw:
  ZFUNC ALWAYS, writes on, no colour channel written, with the Z
  buffer's own memory named as the colour target so the CB has something
  to point at. SDMA's linear rows would land on the wrong samples of the
  1D tiles, so the clear cannot go through SDMA.
- **Mip chains.**
  - The T# gets BASE_LEVEL, LAST_LEVEL and POW2_PAD.
  - The S# gets MAX_LOD and MIP_FILTER.
  - `v9x_vi_3d_mip_layout` places the levels as measured below.
  - `create_surface` puts a chain in one block.
  - The draw counts the levels that sit where the layout puts them, as
    Cedar's does.
  - Every Direct3D mip filter is offered.

## What ran

**Boot 22: T7 (SRCALPHA blend) failed in a pattern.** V9XD32 dumped
T7's first 8x8 pixels (`T7Row0` to `T7Row7`). Only one pixel in four
held the blend, `00807F00`. The rest held pieces of it: `00004080`,
`00FF4080`, `00007F00`. The 32 bpp average was `003F5645` against
`00807F00` expected. T6 (ARGB1555) passed.

**Boot 23: `CB_COLOR_CONTROL` DISABLE_DUAL_QUAD did not fix it.** The
dump was the same pattern, average `003F5744`. The bit stays set while
blending, because it does no harm. It is not the fix.

**Boot 24: the probe isolated it.** V9XCZ3D `/blend` is T7 as one draw.
`BlendDiffer=768`: three pixels in four were wrong. Each wrong pixel was
the right one shifted by 16 bits, for example `Row0X1=0000BF80` where
`BF807F00` belonged. So the CB was taking a 32_ABGR export (four 32-bit
floats) into an 8-bit UNORM target in 16-bit pieces once blending was
on. Without blending the same export had been exact since Phase 4.

The probe was then switched to a compressed FP16 export, which is what
Mesa's radeonsi uses for an 8-bit UNORM target:
- `v_cvt_pkrtz_f16_f32` packs the four channels into two VGPRs;
- `exp mrt0 compr`;
- `SPI_SHADER_COL_FORMAT` FP16_ABGR.

With it, `/blend` and `/blendspec` gave `BlendDiffer=0` and every pixel
read `BF807F00`. Gouraud, depth, texture and modulate passed again
through the same export:
- `GouraudBad=0`;
- `DepthColour` and `DepthBuffer` PASS;
- `TextureExact=1024` for both texture and modulate.

The rectangle probe keeps 32_ABGR.

**Boot 25: V9XD32 PASS at 32 and 16 bpp** (`Failures=0`), with the FP16
export in the HAL. V9XDDP passed:
- specular Gouraud and specular texture;
- vertex fog (texture and modulate);
- all six table-fog samples;
- the vertex-alpha blend;
- ARGB4444 and the colour key.

It failed the untagged texture: `D3DUntaggedTextureRaw=0` where white
belonged.

**Boot 26: the untagged texture was RGB565 with alpha read as 0.** The
T# selected the format's W, and 5_6_5 has no W, so it read 0. MODULATE
then multiplied the result to black. It now selects SQ_SEL_1 for W on a
565 texture, as Cedar does. `D3DUntaggedTextureRaw=992`, `Ok=1`. V9XD32
PASS at both depths again: T7 averages `00807F00` at 32 bpp and `847D00`
(the 16 bpp read-back) at 16.

**Boot 27: where the sampler finds a chain's levels.** V9XCZ3D
`/mip:N` samples a 64x16 A8R8G8B8 chain at level N alone (BASE_LEVEL =
LAST_LEVEL = N). The texture is a pattern in which each dword's value
numbers its position, so each pixel read back gives the byte offset of
the texel the sampler used. All seven levels gave `Mip=PASS`: every
pixel was the texel that the level's origin and pitch predict.

| Level | Size | Origin (bytes) | Row pitch (bytes) |
|---|---|---|---|
| 0 | 64x16 | 0 | 256 |
| 1 | 32x8 | 4096 | 128 |
| 2 | 16x4 | 5120 | 64 |
| 3 | 8x2 | 5376 | 128 |
| 4 | 4x1 | 5632 | (one row) |
| 5 | 2x1 | 5888 | (one row) |
| 6 | 1x1 | 6144 | (one row) |

Level 3 rules out the simplest model, rows padded to 64 bytes: its rows
are 128 bytes for 32 bytes of texels. Levels 4 to 6 are 256 bytes apart
for 16, 8 and 4 bytes. The rule that fits every level:
1. each row is a multiple of 64 bytes;
2. the row is widened 64 bytes at a time until the level is a whole
   number of 256-byte blocks;
3. the levels follow one another with no further alignment.

**Boot 28: mips through the HAL.** V9XDDP's mip checks now pass:
- `D3DMipmapLevelSelectOk=1`, raw 31;
- `D3DTrilinearBlendOk=1`, raw 850;
- `ChainA` to `E` and `ChainRows`;
- `MipLadder` and `MipLadderShape`;
- `MipTri`.

At boot 26 all of these read 0. V9XD32 still passes at 32 and 16 bpp.

## What the evidence says

- **A blended 8-bit UNORM target needs the compressed FP16 export on
  this chip.** The 32_ABGR export was exact without blending and wrong
  with it, three pixels in four.
- **DISABLE_DUAL_QUAD is not the cause.** Setting it left the pattern
  unchanged.
- **The layout rule rests on one chain, at 32 bpp, power-of-two sides.**
  - The 16 bpp case follows from the rule; it was not measured on its
    own.
  - POW2_PAD's padding of other sizes was not measured, so the layout
    refuses a chain whose sides are not powers of two. DirectDraw places
    such a chain and only its level 0 is sampled.
  - V9XDDP's chains at 16 bpp passing is evidence for the 16 bpp case,
    not a measurement of it.

## Still 0 in V9XDDP at boot 28

Each of these was 0 at boot 26 too. None was investigated here.

- The first `D3DZ` and `D3DZP` blocks return `0x88760231` throughout.
  Cedar's do the same. The second `D3DZ` block passes.
- `Mixed*`.
- `Tex8X8888Halves`, `Tex8DecalAlpha565` and `Tex8DecalAlpha4444`:
  raw 0.
- `Persp` and `FlipPixel`.
- `MipTriDegraded`. That check expects the degraded, no-blend result,
  so 0 beside `MipTriOk=1` is what real trilinear gives.

No game has run on this build yet.

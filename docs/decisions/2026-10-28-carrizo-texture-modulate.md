# Carrizo samples an A8R8G8B8 texture through a T# and S# in user SGPRs: point-sampled copy and modulate exact

Date: 2026-10-28

Status: **measured, one machine, two runs of each; passes.** M-XP7053's
Radeon R7E (`1002:9874`), the same session as [depth](2026-10-28-carrizo-depth.md).
This is step 3 of Phase 4 of [the 3D plan](../plans/ati-carrizo-gfx8-3d.md),
and with it Phase 4 is done. Reports are in
[`../probe/m-xp7053-carrizo-texture-2026-10-28/`](../probe/m-xp7053-carrizo-texture-2026-10-28/).

## Sources

- The descriptor layouts are `SQ_IMG_RSRC_WORD0-7` and
  `SQ_IMG_SAMP_WORD0-3`, from Mesa 17.3.9's `src/amd/common/sid.h`. It is
  in `build\external\carrizo\mesa\` (SHA-256 `822ec08a…dcd64`); the
  `mesa3d` GitHub mirror has no tags, so it came from freedesktop's
  GitLab.
- The `image_sample` encoding is from LLVM 17.0.6's `mimg.s`, GCN lines.

## What was built

- **The TL VS** also exports parameter 1 = (tu, tv, 0, 1). Every TL draw
  now sets two parameters:
  - `SPI_VS_OUT_CONFIG` `VS_EXPORT_COUNT` 1 (bits 5:1);
  - `SPI_PS_IN_CONTROL` 2;
  - `SPI_PS_INPUT_CNTL_0/1` reading parameters 0 and 1.

  The rectangle keeps one parameter.
- **Two pixel shaders**, `v9x_vi_shader_texture_ps` and
  `_modulate_ps`.
  - User SGPRs s0-s7 hold the T# and s8-s11 the S#, so PRIM_MASK is s12.
  - They interpolate parameter 1's (u, v) and run
    `image_sample v[4:7], v[2:3], s[0:7], s[8:11] dmask:0xf`, then
    `s_waitcnt 0`.
  - Modulate also interpolates parameter 0 and multiplies, both operands
    VGPRs.
- **The descriptors**, built by `v9x_vi_3d_tl_draw` when a texture is
  given:
  - T#:
    - the address >> 8;
    - `IMG_DATA_FORMAT_8_8_8_8`, UNORM;
    - width and height less one;
    - `DST_SEL` Z, Y, X, W, because A8R8G8B8's bytes are B, G, R, A;
    - the tiling index;
    - `SQ_RSRC_IMG_2D`;
    - the pitch less one.
  - S#: `CLAMP_LAST_TEXEL` on X, Y and Z, LOD 0 to 0, point or bilinear.
  - The PS's VGPR count is now per draw; modulate uses 12.
- **Tests.** The new encoder is checked against LLVM's bytes, both
  shaders word for word, and the descriptor dwords in the stream. They
  were written first and failed to build until the code existed.
- **`V9XCZ3D /texture` and `/modulate`.**
  - A 64x16 A8R8G8B8 texture, linear-aligned (tile index 8), at window
    offset E000h.
  - It is filled by the VxD's pattern op (texel n = seed + n x
    9E3779B9h), because the data array has no room for 4 KiB more.
  - It is mapped 1:1 with point sampling, u from 0 to 1 across, v from 0
    to 1 down.
  - Modulate's diffuse is `FFFF8040`.

## What ran

| Run | Draw | Result |
|---|---|---|
| 1 | `/texture` | all 1,024 pixels exactly the texel under them |
| 2 | `/modulate` | all 1,024 exactly round(texel x diffuse / 255) |
| 3 | `/gouraud`, regression | all within +-2 |
| 4 | `/depth`, regression | colour and Z buffer exact |
| 5, 6 | `/texture`, `/modulate` again | the same as runs 1 and 2 |

Texel 0, `7E57AB1E` (A 7Eh, R 57h, G ABh, B 1Eh), lands as `7E1EAB57`:
R, G, B, A from the low byte. Under modulate it becomes `7E085657`. That
is R 57h, then G ABh x 80h / FFh = 56h, then B 1Eh x 40h / FFh = 08h, with
A unchanged.

## What the evidence says

- **The swizzle and the descriptors are right as built.** The copy is
  exact in every channel, so the T#'s `DST_SEL` maps A8R8G8B8 to RGBA,
  and width, height, pitch, format and type are read as intended.
- **Point sampling at pixel centres lands on texel centres.** The copy is
  exact, with no neighbouring texel anywhere, so u and v reach the
  sampler interpolated correctly.
- **Modulate's arithmetic rounds as the reference.** It matched
  round(a x b / 255) exactly on every pixel, so the +-2 tolerance was not
  needed.

## Not checked

- Bilinear filtering, which the builder can set but no draw used.
- Wrap or mirror addressing, and mipmaps.
- Tiled textures, other formats (565, 1555, 4444, DXT), and a texture
  whose size differs from the target's.
- A texture at a non-zero `BASE_ADDRESS_HI`. The window is below 1 TiB,
  so that field is 0 here.
- rhw other than 1 with a texture, which is what perspective correction
  would show.

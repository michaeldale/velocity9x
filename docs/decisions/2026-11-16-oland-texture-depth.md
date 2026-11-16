# Oland: textures, modulate and Z16 depth exact on the first run

Date: 2026-11-16

Status: **measured, one run each.** W9QI0DAD (Oland XT `1002:6610`),
boot 49, ati Oland row, desktop 1024x768x16. Step 2 of
[`../plans/ati-oland-gfx6-3d.md`](../plans/ati-oland-gfx6-3d.md), the
last of V9XCK3D's first draws. Evidence:
[`../probe/oland-w9qi0dad-2026-11-16/`](../probe/oland-w9qi0dad-2026-11-16/)
`sicp-texture-1`, `sicp-modulate-1`, `sicp-depth-1`, and the regression
runs `sicp-gouraud-tex`, `sicp-rect-tex`, `bltx-after-texdepth-16bpp`.

## What changed

- **Depth, GFX6 mode of `vi_3d.c`.** radeonsi's GFX6 depth surface sets
  no array mode, pipe configuration or banks in DB_DEPTH_INFO, only
  ADDR5_SWIZZLE_MASK; the DB takes the rest from the tile index in
  DB_Z_INFO. Oland's 1D depth tile mode is GB_TILE_MODE4 (depth micro
  tiling, 1D_TILED_THIN1, P4_8x16); its mode 5, Carrizo's and Mullins'
  1D depth index, is 2D on Oland. So in GFX6 mode DB_DEPTH_INFO is 1 and
  DB_Z_INFO 00400001h (Z16, index 4). A linear Z buffer is refused in
  GFX6 mode: that path was measured on Mullins only.
- **Textures: nothing.** The T# and S# fields the builder writes sit at
  the same places on SI as on CI. Tile index 8 is linear-aligned in
  Oland's table too, as the colour target already showed.
  `image_sample` and the texture and modulate pixel shaders go through
  `v9x_si_shader_from_vi` unchanged.
- **V9XSICP** gains V9XCK3D's `/texture`, `/modulate` and `/depth`, with
  their vertices, data and checks. The TL vertex shader is the MUBUF one
  ([`2026-11-16-oland-gouraud-mubuf-fetch.md`](2026-11-16-oland-gouraud-mubuf-fetch.md)).

## Results

| Run | What it checks | Result |
|---|---|---|
| `/texture` | a 64x16 A8R8G8B8 pattern texture mapped 1:1, point-sampled: every pixel the texel under it, byte for byte | **PASS**, 1,024 of 1,024 exact (texel 0 7E57AB1E = pixel 0) |
| `/modulate` | the texel times a D3DCOLOR (R FFh, G 80h, B 40h, A FFh), within +-2 per channel | **PASS**, all 1,024 within, and all 1,024 exactly round(a x b / 255) |
| `/depth` | Z16 cleared to 1.0; red at z 0.5 over all, green at 0.75 over the left half (behind), blue at 0.25 over the right (in front), LESS with writes | **PASS**: red left, blue right, 0 bad pixels; every Z16 tile 8000h on the left, 4000h on the right |

Every fence landed (10-12 polls). After all three, `/gouraud` and `/rect`
pass again, and V9XBLTX is byte-exact with no engine timeouts.

## What this settles

V9XCK3D's whole first-draw set now passes on GFX6: solid, Gouraud,
texture, modulate and depth. Every instruction those programs use runs
on GCN 1 from the CI words (FLAT aside), and the T#, S#, the Z16 1D
depth layout and the DB's GFX6 setup work as built.

Not covered: bilinear filtering, mip chains (`/mip`), blending
(`/blend`), seams, the tiled-Z sample order inside a tile (`/ztile`), and
any tiled colour target. Whether the chip holds the partly-reading
`GB_ADDR_CONFIG` is still open, though a 1D-tiled Z buffer working
correctly argues it is not badly wrong.

## Next

Step 4 of the plan, the Direct3D HAL: the Oland row stamps
`V9X_DD_ENGINE_CAP_D3D`, and `vi_cp.c` and `d3d_vi.c` run in an Oland
mode, with SI's CP bring-up (as V9XSICP does it), SI's start stream, the
GFX6 builder flag and the SI shader paths, until V9XD32 passes at 16 and
32 bpp.

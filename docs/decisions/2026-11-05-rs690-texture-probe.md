# RS690 textures by register writes: nearest, wrap, modulate, bilinear

Machine: M-GB8KRE (Sempron 210U, Radeon X1200 IGP 1002:791F, Win98 SE),
on the branch `ati` driver (13103c2d-dirty, boot 56, no reboot since the
draw probe). Probe `V9XRS3D.EXE` + `RS3D.VXD` (VxD unchanged) from branch
`ati-rs690` at 6c3ef3d9 plus the uncommitted 3D work (`6c3ef3d9-dirty`).
2026-11-05 ~07:00Z. Captures in `docs/probe/rs690-3d-tex-2026-11-05/`: the
result file and each texture test's 4 KiB read-back surface (`R3D` decal,
`R3W` wrap, `R3M` modulate, `R3B` bilinear, `R3A` the colour-only quad
after them), run 1 at both depths and run 2 at 32 bpp.

## Question

Second step of phase 5 (`docs/plans/ati-rs690-x1200.md`): with the CP
still off, does texture unit 0 sample a linear texture in the frame buffer
where the model says, and does the shader's texture load combine with the
interpolated colour? This is what the Direct3D HAL needs before anything
else in the texture path.

## What ran

`v9x_rs690_build_3d_texture` in `src/chipsets/ati/rs690/rs690_3d.c`
(host test written first, failed against a stub): 31 writes after the
colour-buffer state. Unit 0 as xf86-video-ati 6.14.6's R300TextureSetup
(TX_FILTER0/1, TX_FORMAT0 with TXPITCH_EN, TX_FORMAT1 from R300TexFormats,
TX_FORMAT2 pitch, TX_OFFSET, border 0, TX_INVALTAGS, TX_ENABLE); s, t as a
FLOAT_2 stream into vector slot 6 as the X driver sends them; the
rasterizer puts s, t in shader temp 0 and colour 0 in temp 1 (Mesa's
COL_ADDR field); the shader is one `LD temp0, temp0, unit 0` and one ALU
instruction, `out = temp0` (REPLACE) or `out = temp0 * temp1` (MODULATE).
Vertices are eight dwords.

The texture is 32 x 16 texels in the surface's format (RGB565 or
ARGB8888), written by the VxD's gradient op 64 KiB past the test surface:
at 16 bpp texel n is n (blue = column, green = row), at 32 bpp
2n | (2n + 1) << 16. Each test is one VxD run: prefill, texture, wait
idle, state, draw, finish, wait idle, read back.

## Measured

| | 640x480x16 | 1024x768x32 |
|---|---|---|
| TexDecal: 1:1, nearest, clamp, REPLACE: bytes off | 0 | 0 |
| TexWrap: s 0 to 2 over 64 pixels, nearest, wrap: bytes off | 0 | 0 |
| TexModulate: texel x (160, 128, 96, 255): worst channel error | 0.88 step | 0.52 step |
| TexBilinear: 2x, linear, clamp: worst channel error | 0.75 step | 3.50 steps |
| QuadAfterTex: colour-only quad after the target state again: bytes off | 0 | 0 |
| FillAfter3D: bytes off | 0 | 0 |

Every wait finished in 2 to 28 status reads; `RBBM_STATUS` 10000140h at
the end. `GB_TILE_CONFIG` read 00000011h at start, so the init from the
earlier draw probe survived about two hours of desktop use.

Run 1 at 32 bpp reported TexBilinear MISMATCH: 126 of 1024 pixels over
the probe's one-step tolerance. The capture (`R3B-run1-1024x768x32.BIN`)
puts every one of them in pixel rows 7, 8 and 15. Those are the rows
that blend texel rows 3/4 and 7/8, where this texture's 32 bpp pattern
jumps about 192 steps. Elsewhere no channel is off by more than 1.0
step. Solving the blue channel for the vertical weight on those rows gives
0.232 or 0.247 where the model has 0.25, alternating by column. Rows 8
are 0.732 / 0.747 for 0.75. So the filter takes the right four texels
and clamps correctly; its weights are coarser than exact, by up to 0.018.
A wrong texel would be off by about half the 192-step spread.

The probe now allows one step plus 1/32 of the four texels' spread for
bilinear. Run 2 (32 bpp only) passed with the same 3.50-step worst case;
the decal, wrap, modulate and bilinear surfaces of run 2 are byte-identical
to run 1. The 16 bpp result is from run 1, under the stricter tolerance.

## Hypotheses this disposes of

- That TX_OFFSET wants something other than the MC address the colour
  buffer uses: 78000000h + offset sampled the right texels.
- That a texture coordinate past 1 needs anything beyond WRAP (0) in
  TX_FILTER0: s = 2 repeated the texture exactly.
- That colour and texture coordinates cannot share interpolator 0: both
  arrived (MODULATE within a step).

## Not checked

- Minification and mipmaps (TX_FORMAT0 NUM_LEVELS, the MIP filter bits).
- Formats other than RGB565 and ARGB8888 (1555, 4444, 332, palettized,
  DXTn), and a texture in a format other than the colour buffer's.
- Alpha: blending, alpha test, a texture's alpha into MODULATE beyond the
  0/1 alpha of the 32 bpp gradient.
- Perspective-correct coordinates (no W sent yet), Z, culling, fog.
- Textures in AGP or system memory; only the carve-out was used.
- Throughput.

# SiS 6326 3D registers caught live under SiS's HAL

Date: 2026-10-04. Machine: A8U4I5 boot 207, card 2 (rev 0Bh, subsystem
`63261569`), SiS/AOpen driver 2.28. Follows
[the first survey](2026-10-04-sis6326-first-survey.md).

Evidence is in `docs/probe/a8u4i5-sis6326-registers-2026-10-04/`:

- `V9XDD-CARD2-SIS228-B207.INI`: V9XDDP's battery against SiS's HAL.
- `SIS6326-G-CARD2-AFTER-D3D-B207.TXT`: the register probe after V9XDDP
  exited.
- `SIS6326-H-CARD2-DURING-D3D-B207.TXT`: the register probe about 10 s into
  a detached V9XDDP run.

## Measured

**SiS's HAL is real and renders at 16 bpp only.** The HAL device reports:

- render depth and Z depth both `1024` (16 bpp only), colour model RGB;
- source and destination blend masks 8191;
- 1024 maximum vertices.

V9XDDP switched to exclusive 640x480x16 and passed:

- triangle pixel, orientation and sub-pixel checks;
- 12 of 12 shapes;
- Gouraud specular, fog with texture, depth fog, vertex alpha blend;
- base, tiled and negative-coordinate textures;
- mip level selection and trilinear blending;
- 4444 and 565 textures, colour key;
- texel sizes 2-256, modulate, bilinear, decal, clamp, LOD;
- texture chains A-D.

It failed:

- every Z-buffer test, HRESULT `88760231h` from surface creation onward;
- `PerspOk`, `MipLadderShapeOk`, `MipTriDegradedOk`, `TexMatrixCountsOk`;
- the mixed colour/depth test, which needs Z.

**After V9XDDP exits, 3D is off again** (SR39 = 00h; desktop back at 8
bpp). **During the run** (capture H):

| Register | Value | Reading |
|---|---|---|
| SR39 | 04h | 3D enabled |
| SR3C | 43h | Turbo Queue split 3: 2D 4K, 3D 28K |
| SR27 / SR06 | D0h / CAh | 16 bpp mode, logical width code 1 |
| 89FCh | idle, queue empty | 30208 bytes free |
| 8A00h | 00208CA0h | Z write, prim setup, texture, texture cache, large cache, **bit 15** (datasheet: reserved) |
| 8A04h | 00030000h | Z8, LEQUAL, pitch 0 (Z test off) |
| 8A0Ch | 07000000h | alpha test always |
| 8A14h | 0C110080h | ROP Ch (copy), RGB565, pitch 128 |
| 8A18h | 00196900h | destination base |
| 8A20h | 010000FFh | normal fog, colour 0000FFh |
| 8A28h | 01000000h | source ONE, destination ZERO |
| 8A30h / 8A34h | 0000003Fh each | clip top 0 / bottom 63, left 0 / right 63 |
| 8A38h | 53030100h | ARGB4444, wrap U+V, level field 1, nearest/nearest |
| 8A44h | 0019DC00h | texture level 0 base |
| 8A6Ch | 02800200h | level 0 pitch 280h, level 1 pitch 200h |
| 8A80h | 66800000h | 64 x 64 |

Vertex registers, decoded as IEEE singles:

| | X | Y | Z | U | V | W | ARGB |
|---|---|---|---|---|---|---|---|
| a | 8.25 | 55.75 | 0 | 0.6 | 0.4 | 1.08e-19 | FFFFFFFF |
| b | 55.75 | 8.25 | 0 | 0.9 | 0.1 | 1.79e-43 | FFFFFFFF |
| c | 8.25 | 8.25 | 0 | 0.6 | 0.1 | 1.0 | FFFFFFFF |

89F8h = `00118682h`: triangle, Gouraud, top b, middle c, bottom a, fire
position 6 (after the write of TSWc), TDRAWDIR = 1.

## What the evidence settles

1. **Vertex X, Y, U and V are IEEE-754 single precision**, written unscaled
   in screen pixels and texture coordinates.
2. **The driver sorts by Y and names the order in 89F8h.** Top is the
   smallest Y; ties in Y take the order a driver chooses. The hardware does
   not sort.
3. **SiS fires on TSWc** (TSETFIRE = 6): every vertex register is written,
   and the last write starts the engine; no separate TFIRE write.
4. **The datasheet's destination format (8A14h D[22:16] = 11h for RGB565),
   blend nibble order, clip packing (13-bit fields, inclusive bounds) and
   texel code 53h (ARGB4444) are correct.**
5. **SiS turns 3D on only while a Direct3D client holds a 16 bpp mode**, and
   gives the Turbo Queue 28K of 32K to 3D while it does.

## Not established

- **TDRAWDIR's rule.** One sample: middle vertex left of the long edge,
  TDRAWDIR = 1.
- **The texture pitch unit.** 280h and 200h for a 64-texel ARGB4444 level
  are neither its 128 bytes nor its 64 texels. These may also be values left
  by an earlier texture in the battery.
- **Enable bit 15**, set by SiS and reserved in the datasheet.
- **Why every Z test fails under SiS's own HAL.** The HRESULT is recorded;
  it is not decoded here.
- **Whether W is RHW.** Perspective was off in the captured triangle.

# RS690 Direct3D state by register writes: TL vertices, Z16, blend, alpha test, cull, perspective

Machine: M-GB8KRE (Sempron 210U, Radeon X1200 IGP 1002:791F, Win98 SE),
on the branch `ati` driver (13103c2d-dirty, boot 56). Probe `V9XRS3D.EXE`
+ `RS3D.VXD` (VxD unchanged) from branch `ati-rs690` at 066ce495 plus this
work. 2026-11-05 ~08:00Z, two runs at 640x480x16 and 1024x768x32.
Captures in `docs/probe/rs690-3d-state-2026-11-05/`: the result file and
the surfaces of the new tests (`R3H` TL Gouraud, `R3Z` Z, `R3L` blend,
`R3K` alpha test, `R3P` perspective, `R3A` the colour-only quad after).

## Question

Third step of phase 5 (`docs/plans/ati-rs690-x1200.md`), still with the CP
off. The draw and texture probes ran the X driver's vertex format; a
Direct3D HAL wants the D3DTLVERTEX the runtime hands it, and the render
state every period game sets. Does the chip take that vertex and that
state as the sources encode them?

## What ran

`v9x_rs690_build_3d_state` in `src/chipsets/ati/rs690/rs690_3d.c` (host
tests written first, 78 failing against stubs). It writes the probe-proven
colour-buffer and texture sequences, then what differs:

- **Vertex.** Eight dwords, D3DTLVERTEX as it is, Mesa r300's
  software-TCL layout:
  - sx, sy, sz, rhw as FLOAT_4 into slot 0. VTE's W0 bit is clear, so the
    fourth component is 1/W.
  - Diffuse and specular as BYTE + NORMALIZE into colour slots 0 and 1,
    swizzled from the D3DCOLOR byte order B, G, R, A.
  - tu, tv as FLOAT_2 into slot 6.
- **Z16.** Linear, at its MC address: ZB_FORMAT 0, ZB_DEPTHOFFSET,
  ZB_DEPTHPITCH in pixels, ZB_ZSTENCILCNTL compare, ZB_CNTL enable and
  write.
- **Blend.** RB3D_CBLEND and ABLEND: enable, READ_ENABLE, ADD_CLAMP, GL
  factor codes.
- **Alpha test.** FG_ALPHA_FUNC: reference, compare, enable.
- **Cull.** SU_CULL_MODE bits with the init's FACE_NEG.
- **Texture alpha.** Taken from the texel, the diffuse colour, or both
  multiplied.

The finish also flushes the Z cache. The colour-only target state now
turns Z, the alpha test and culling off, so it means the same after a
TL state that set them.

## Measured

Two runs per depth. The first run's Perspective failed on a fault in
the test, explained below; its other new captures are byte-identical
to the second run's.

| | 640x480x16 | 1024x768x32 |
|---|---|---|
| TlGouraud (TL vertex, packed colours): wrong inside/outside | 0 / 0, 0.66 step | 0 / 0, 0.59 step |
| ZTest: three overlapping quads, LESS, writes: bytes off | 0 | 0 |
| Z16 read back at z = 0.25 / 0.5 / 0.75 / cleared | 4000h / 7FFFh / BFFFh / FFFFh | the same |
| Blend SRCALPHA/INVSRCALPHA, alpha 96: worst channel error | 0.14 step | 0.88 step (alpha included) |
| AlphaTest GREATER 128: alpha 100 dropped, 200 kept: bytes off | 0 | 0 |
| CULL_FRONT removes / CULL_BACK removes | clockwise / counter-clockwise | the same |
| Perspective, rhw 1 to 0.25: judged pixels on the perspective column | 992 of 992 (affine would be 48) | 992 of 992 |
| QuadAfterTex with the colour-only state after all of it: bytes off | 0 | 0 |
| FillAfter3D: bytes off | 0 | 0 |

The TL Gouraud's worst errors and pixel samples are the same as the
X-driver-format Gouraud's. Normalized bytes therefore give the same
colour as the floats did.

Clockwise is as the screen shows it, y down. Direct3D's default
D3DCULL_CCW removes counter-clockwise triangles, so it maps to
CULL_BACK and D3DCULL_CW to CULL_FRONT.

## The first perspective run, and what it disposes of

Run 1 drew the quad with tv from 0 to 1 down its height while rhw went
from 1 to 0.25 across it, and judged each pixel against its own texel
row. 757 of 992 pixels sampled the wrong row. Decoding the 16 bpp
capture (texel n is stored as n) shows two things:

- Every column was the perspective one, in every row.
- The rows bent along the quad's diagonal.

A quad whose rhw varies across it is not the projection of any
rectangle, so its two triangles interpolate a varying tv differently.
The expectation was wrong, not the chip. Run 2 holds tv at 0 and
passes. So perspective correction needs no RS_W_EN or other switch:
1/W in the fourth position component is enough, as Mesa's
software-TCL path has it.

## Not checked

- The specular colour is routed to colour slot 1 but no shader reads
  it. Fog is not built either.
- Z compare functions other than LESS. Z with writes off. Z24.
- Blend factor pairs other than SRCALPHA/INVSRCALPHA. Destination alpha
  on a 565 target.
- Alpha test functions other than GREATER.
- Throughput: a TL triangle is 25 register writes after the state. The
  state is at most 87 writes, some of them repeats.
- Mipmaps, other texture formats, colour keying.

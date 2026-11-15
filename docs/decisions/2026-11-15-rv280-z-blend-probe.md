# RV280 Z, blending and alpha test by register writes

Date: 2026-11-15

Machine: M-HNBJNV, Radeon 9250 (`1002:5960`), Windows 98 SE. The driver
was branch `ati-rv280`'s ati package (`539fb5ce-dirty`, boot 19). Probe:
`V9XR23D.EXE` + `R23D.VXD` at `f5e55f5c-dirty`. Phase 5 of
[`../plans/ati-rv280-radeon-9250.md`](../plans/ati-rv280-radeon-9250.md),
after [textures](2026-11-15-rv280-texture-probe.md). Evidence:
[`../probe/rv280-z-blend-probe-2026-11-15/`](../probe/rv280-z-blend-probe-2026-11-15/),
from the second run.

## What ran

`src/chipsets/ati/rv280/rv280_3d.c`'s target state gains an optional
raster state, `struct v9x_rv280_3d_raster`, in the hardware's own codes.
Offsets and fields are from xf86-video-ati 6.14.6's `radeon_reg.h`. No
R200 path in the X driver writes any of these:
- **Depth:**
  - `RB3D_DEPTHOFFSET` and `RB3D_DEPTHPITCH` (pixels);
  - `RB3D_ZSTENCILCNTL`: 16-bit integer Z, Z_TEST, Z_WRITE_ENABLE;
  - `RB3D_CNTL`'s Z_ENABLE;
  - Z0 in `SE_VTX_FMT_0`, which puts Z after X and Y in the vertex.
- **Blending:** `RB3D_BLENDCNTL`'s factor codes and `RB3D_CNTL`'s
  ALPHA_BLEND_ENABLE.
- **Alpha test:** `PP_MISC`'s reference and function, and `PP_CNTL`'s
  ALPHA_TEST_ENABLE.
- **Read-back:** the finish can flush the Z cache, `RB3D_ZCACHE_CTLSTAT`
  ZC_FLUSH_ALL, so the probe can read the Z buffer.

The VxD's register window grew from 325Ch to 3254h-325Fh to take that
flush.

The probe's new tests:
- **ZUnit / ZBuffer:** three overlapping quads with Z16 LESS and writes:
  red at z 0.5, green at 0.25 over the right, blue at 0.75 over the
  middle. No source says what range a vertex Z is in without the
  viewport transform (`SE_VTE_CNTL` 0), so ZUnit runs with z in 0-1 and
  ZBuffer with z times 65535.
- **Blend:** SRCALPHA / INVSRCALPHA, a red quad with alpha 80h over the
  A5h prefill.
- **AlphaTest:** GREATER 128, quads with alpha 200 and 100.

## First run: alpha was 0

At both depths, ZUnit passed, ZBuffer drew nothing, and blending left
the prefill as it was, every pixel `A5A5` / `A5A5A5A5`: the source alpha
was 0. The alpha test dropped both quads. The cause was `SE_CNTL`'s alpha
shade model, bits 11:10. The init writes the X driver's value, which
sets only the diffuse shade model; alpha stays SOLID. Since this run,
the target state sets alpha FLAT or GOURAUD to match the colour
(`9800051E` flat, `98000A1E` Gouraud), as `RADEON_ALPHA_SHADE_*`
(`radeon_reg.h:2271-2274`) provides. The host test's four `SE_CNTL`
checks were changed first and failed against the old builder.

That also explains the alpha byte of 0 at 32 bpp in the
[triangle probe](2026-11-15-rv280-3d-draw-probe.md). Since the fix the
flat red pixel reads `FFFF0000`.

## Second run, measured

| Test | 1024x768x16 | 1024x768x32 |
|---|---|---|
| ZUnit: z in 0-1, nearest on top in every column | 0 wrong | 0 wrong |
| ZBuffer: z × 65535 | nothing drawn | nothing drawn |
| Blend, SRCALPHA / INVSRCALPHA, alpha 80h | 0 wrong, worst error 0 | 0 wrong, worst error 0 |
| AlphaTest, GREATER 128: alpha 200 kept, 100 dropped | 0 wrong | 0 wrong |
| Gouraud, textures, 2D fill after | PASS | PASS |

- **The vertex Z is in [0, 1].** It maps to the 16-bit buffer as
  z × FFFFh: the buffer holds `8000` where z was 0.5 and `4000` where it
  was 0.25. A Z of 32767 or more fails LESS against a cleared buffer, so
  ZBuffer drew nothing. Direct3D's TL vertices carry sz in 0-1 already.
- **The Z buffer is not linear in memory.** Reading the first row as 64
  consecutive 16-bit values, column 8 held `FFFF`, column 30 `8000` and
  column 60 `4000`. A linear layout would give `8000`, `4000` and `FFFF`.
  The R100/R200 family keeps its depth buffer tiled, which fits, but the
  layout was not decoded. The colour result is what proves the depth
  test. A DirectDraw Lock of a Z buffer would see the tiled layout, as on
  the GT218.
- **Blending is exact** against `(src × 128 + dst × 127) / 255` per
  channel.

## Not checked

- 24-bit Z and stencil, other Z functions, and Z without writes.
- Blend factors other than SRCALPHA / INVSRCALPHA.
- Culling, fog, specular and perspective (RHW).
- How the Z buffer is tiled.

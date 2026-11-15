# RV280 culling, perspective, fog and specular by register writes

Date: 2026-11-15

Machine: M-HNBJNV, Radeon 9250 (`1002:5960`), Windows 98 SE. The driver
was branch `ati-rv280`'s ati package (`539fb5ce-dirty`, boot 19). Probe:
`V9XR23D.EXE` + `R23D.VXD` at `44fb27d3-dirty`. Phase 5 of
[`../plans/ati-rv280-radeon-9250.md`](../plans/ati-rv280-radeon-9250.md),
after [Z and blending](2026-11-15-rv280-z-blend-probe.md). Evidence:
[`../probe/rv280-cull-rhw-fog-probe-2026-11-15/`](../probe/rv280-cull-rhw-fog-probe-2026-11-15/),
from the last run.

## Sources

Mesa 20.3.5's classic `r200` driver, fetched into
`build\external\rv280\mesa-r200\` (`SOURCE.txt` there). Its software-TCL
path is what Direct3D's TL vertices need:
- **Vertex order:** X, Y, Z, W, colour, specular-and-fog, texture
  coordinates (`r200_swtcl.c:100-170`).
- **Pre-divided coordinates:** `SE_VTE_CNTL` = `VTX_XY_FMT` | `VTX_Z_FMT`
  with `VTX_W0_FMT` clear, so the W field is 1/W, and `SE_VAP_CNTL`
  without `FORCE_W_TO_ONE` (`r200_swtcl.c:259-269`).
- **Culling:** `r200CullFace` and `r200FrontFace`: `SE_CNTL`'s
  `FFACE_CULL_CCW` (bit 0) and its `BFACE_SOLID` / `FFACE_SOLID` fields.
- **Fog and specular:**
  - `PP_FOG_COLOR` holds the fog RGB, with vertex fog and the factor
    taken from the specular alpha (`FOG_USE_SPEC_ALPHA`);
  - `PP_CNTL` has `FOG_ENABLE` and `SPECULAR_ENABLE`;
  - `r200ShadeModel` shades alpha, specular, fog and discrete fog the
    same way as colour.

The builder (`src/chipsets/ati/rv280/rv280_3d.c`) adds all of this:
- the target state now names its vertex layout (Z, W, specular);
- the raster state gains the face settings, fog and specular;
- the host test's new checks failed against the old builder first (25
  of them).

## First run: perspective was affine

Cull, fog and specular passed. A texture with 1/W 1 at the quad's left
edge and 0.25 at the right drew affine: texel column x/2 across all 64
pixels. The init's `RE_CNTL` was the X driver's 0. Mesa's initial state
sets `RE_CNTL`'s `PERSPECTIVE_ENABLE` (bit 3, `r200_state_init.c:1001`),
and every draw now writes it; that check failed against the old builder
first.

With the bit set, row 0 matched the perspective model column for column,
but other rows drew other texel rows. That was the test's mistake. A
screen rectangle whose 1/W changes across it is not the image of a flat
rectangle: t/W at its corners (0, 0, 1, 0.25) is not one plane, so each
of the two triangles interpolates its own t. s/W at the corners
(0, 0.25, 0, 0.25) is one plane, so the test now judges the texel column
only.

## Last run, measured

| Test | 1024x768x16 | 1024x768x32 |
|---|---|---|
| Cull, back faces, CW front | screen-CW triangle kept | same |
| Cull, front faces, CW front | screen-CCW kept | same |
| Cull, back faces, CCW front | screen-CCW kept | same |
| Perspective: 1/W 1 to 0.25, texel column at each judged pixel | 0 of 800 wrong; affine would match 16 | 0 of 800 wrong |
| FogFlat: factor 80h toward blue, within a step | 0 wrong, worst 0 | 0 wrong, worst 0 |
| FogGouraud: factor FFh to 0 across the quad | 0 wrong, worst 1 | 0 wrong, worst 1 |
| Specular: 404040h + 202020h = 606060h | 0 wrong, worst 0 | 0 wrong, worst 0 |
| Every earlier test and the 2D fill after | PASS | PASS |

- **The hardware's winding matches the screen's.** Its "clockwise" is
  clockwise on the screen with y down. D3DCULL_CCW, which culls
  counter-clockwise faces, is therefore a clockwise front with back faces
  culled.
- **Perspective-correct texturing works** from D3D's rhw as it is, with
  sx, sy and sz already divided. Pixels within 0.08 of a texel boundary
  were not judged.
- **Fog** blends toward the fog colour by the specular alpha, with FFh
  meaning no fog, as Direct3D defines it. The flat case is exact.
- **Specular** adds to the colour, exact.

## Not checked

- Fog's interaction with textures and blending; table fog.
- Culling with Z and textures on, and degenerate triangles.
- Perspective-correct colour, though Mesa's path implies it.
- Mipmaps, other texture formats and the colour key: the remaining
  texture work before the HAL.

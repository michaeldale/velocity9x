# Haswell Phase F2a: Direct3D draws on the render ring through the HAL; the clipper's divide had to go

2026-10-17, i5-4570 (`8086:0412` rev 06), Windows 98 SE, boots 10 and 11,
the `intel-gma` package installed by Have Disk (boot 10), then V9XHAL.DLL
replaced by a WININIT rename (boot 11). V9XDDP's full Direct3D probe at
its 640x480x16 mode. As designed in `docs\plans\intel-haswell-direct3d.md`
Phase F2. Evidence: `docs\probe\hsw-4570-d3d-f2a-2026-10-17\`.

## What is in the driver

- **The mini-VDD** maps a 128 KB reserve and opens the render ring at
  `1802000h` beside the blitter's. V9XHSWMM read-only after boot 10: RCS
  START `01802000`, CTL 1; BCS START `01800000`, CTL 1; forcewake held.
  `V9XHW.INI`: `EngineCaps=00000013` (fill, copy, D3D),
  `Direct3D=hardware-gen75`, `Direct3DMode=hardware`.
- **`d3d_hsw.c`**, on the neutral draw entry:
  - D3D's TL vertices are copied unconverted, and the vertex elements
    fetch them (sx sy sz rhw, then diffuse and specular as
    B8G8R8A8_UNORM, tu tv).
  - Each draw writes its surfaces, sampler, blend, CC and generated
    kernel into the reserve, then runs one synchronous batch after the
    blitter is idle.
  - The initialising batch runs once per ring open, the first time a
    process finds the reserve without the mark.
  - No Z buffer is advertised.

## Boot 10: perspective broke every scene with rhw other than 1

V9XDDP found the HAL (`D3DHalFound=1`), and its rhw-1 scenes passed.
`PerspOk` and `D3DTableFogOk` failed:

- The W-range scenes, which only scale rhw uniformly, read 0 (nothing
  drawn) at rhw 0.05, 0.5, 2, 4 and 8. Only 1.5 drew.
- The perspective scene stopped drawing a third of the way along its
  edge.

The CLIP unit divides x and y by the position's w even with clipping
disabled, and the TL vertices carry rhw in w. Vol 2b's
`3DSTATE_CLIP` Perspective Divide Disable describes this case: "submit
pre-transformed screen-space geometry ... W component of positions to
contain rhw", with CLIPMODE_ACCEPT_ALL required alongside. TL draws now
emit CLIP with the unit on, ACCEPT_ALL and the divide off (DW2
`80008200h`). The probe batches of C-F keep their all-zero CLIP and w =
1.

## Boot 11: the checks

| | |
|---|---|
| `Ok` checks reporting non-zero | 89 |
| reporting 0 | 22 |
| fixed by the CLIP change | `PerspOk`, `D3DTableFogOk` |

Passing, among the 89: `D3DTrianglePixelOk`, the 12 coverage shapes and
slot permutations, `D3DSubpixelTriangleOk`, Gouraud specular, fog with
and without texture, vertex alpha blending, base, untagged, tiled and
negative-tiled textures, the texture matrix rows. The W range is uniform:
all six rhw scales read the same colour. `PerspMid` reads green and
`PerspFar` blue, which are the perspective-correct answers.

Every failure is in a declared F2a gap:

| failing | why |
|---|---|
| `D3DZ*`, `D3DZP*`, `Mixed*` (8) | no Z buffer (F2b) |
| `D3DMipmapLevelSelect`, `D3DTrilinearBlend`, `Chain*` (6), `MipLadder`, `MipTri*` (2) | mips sample level 0 |
| `ColorKeyHalves` | colour key refused |
| `Tex8DecalAlpha4444`, `Tex8DecalAlpha565` | DECALALPHA refused |

Two texture-matrix rows that passed at boot 10
(`TexM_64_4444_gapped_lin`, `_mipnear`) were not written at boot 11. Not
investigated.

After the probe a V9XDDF fill and copy passed, and V9XTRACE showed no
idle timeout.

## Findings

- The Direct3D path works on the render engine, with all state built by
  the host-tested builders: geometry, coverage, Gouraud, specular, fog,
  vertex alpha, blending, and 16-bit textures with wrap, mirror, clamp,
  nearest and bilinear.
- **Pre-transformed vertices need the divide off.** With rhw in w and the
  CLIP unit's perspective divide on, x and y are divided by rhw. With it
  off, w = rhw gives perspective-correct attributes, as the PRM says.
- The probe phases used w = 1 throughout, which is why their passes could
  not show this.

## Not measured

- Depth (F2b), mipmaps, DECALALPHA, colour key, a game. Performance:
  every draw waits for its batch.
- The V9XTRACE snapshot's `EngineCaps` reads 3 while `V9XHW.INI` reads
  `13`. Not investigated.

## Next

F2b: Z buffers placed in whole Y tiles by `create_surface`, depth fills
drawn by the engine, the depth test and write on in the batch. Then a
game.

# Haswell Phase E: eight texture-stage combinations match the reference, textured and fogged

2026-10-17, i5-4570 (`8086:0412` rev 06), Windows 98 SE, boot 9, with the
B2 driver on the desktop. `V9XHSWMM.EXE /shade:0` to `/shade:3`, two
variants per run in Phase D's layout, as designed in
`docs\plans\intel-haswell-direct3d.md` Phase E. Evidence:
`docs\probe\hsw-4570-shade-e-2026-10-17\`.

## What ran

Every kernel came from one generator (`v9x_intel_hsw_ps_kernel_build`).
Inputs are four attributes (diffuse, specular with the fog factor in
alpha, two texture coordinate pairs), two 8x8 B8G8R8A8 textures, one
sampler (NEAREST, MIPFILTER_NONE, CLAMP), and the SIMD8 `sample` message
without a header (mlen 2, rlen 4). The alpha test is BLEND_STATE's,
GREATER against 80h. The triangle is Phase D's. All four runs: status
`000104FB`, both draws completed, EIR 0.

| Variant | Kernel | Inside judged | Not judged | Discarded | Mismatches | Max error |
|---|---|---|---|---|---|---|
| texture | 8 instructions | 1032 | 137 | 0 | 0 | 0 |
| modulate | 12 | 1032 | 137 | 0 | 0 | 1 |
| add (colour), modulate (alpha) | 12 | 1032 | 137 | 0 | 0 | 1 |
| decal (texture colour, diffuse alpha) | 12 | 1032 | 137 | 0 | 0 | 0 |
| modulate + alpha test | 12 | 1032 | 137 | 528 | 0 | 1 |
| diffuse + vertex fog | 19 | 1103 | 66 | 0 | 0 | 1 |
| modulate, then modulate by texture 1 | 19 | 1015 | 154 | 0 | 0 | 1 |
| diffuse | 9 | 1103 | 66 | 0 | 0 | 1 |

"Not judged" means pixel centres within 1/64 of an edge, or within 1/64
of a texel boundary for the textured variants. Every variant has 2927
outside pixels, all still guard. The alpha test discarded exactly the
pixels the reference says fail, which is the half of the checkerboard
with alpha 40h. Error is in 1/255 steps and the tolerance was 2.

Afterwards the RCS ring registers read 0, the BCS ring was idle and
enabled, forcewake was held, and a V9XDDF fill and copy passed.

## Findings

- **Texture sampling works** as designed: SURFACE_STATE for a linear
  texture, SAMPLER_STATE, the binding table entries and the sampler
  descriptor read from Vol 7 (SIMD mode 18:17, type 16:12). Nothing hung.
  The reference's nearest texel, floor(u x 8) clamped, matched at every
  judged pixel.
- **The arithmetic** (`mul`, `add`, saturate, immediates, `pln` on four
  attributes) gives D3D's stage results within 1/255.
- **Fixed-function alpha test** in BLEND_STATE with the CC reference
  works with the kernel's float alpha.
- **Two samples in one kernel** (two binding table entries, one sampler)
  work.
- Phase D's batch is unchanged by the new draw fields: a host test pins it
  at the 190 dwords that ran.

## Not measured

- Linear filtering, wrap and mirror modes, mipmaps, non-square or large
  textures, other texture formats. These are Phase F.
- Texel-boundary and edge pixels (137 per textured variant): the snapping
  rule there.
- Perspective-correct texturing with w other than 1.
- Fog colour accuracy beyond the tolerance. The reference's fog uses an
  8-bit fog factor, so errors of 1 are expected from it as well as from
  the hardware.

## Next

Phase F: textures and state in full (filtering, wrap modes, blend states,
depth), then the `V9X_D3D_ENGINE_OPS` table. Each needs a design.

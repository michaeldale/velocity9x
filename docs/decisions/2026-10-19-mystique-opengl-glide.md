# OpenGL and Glide on the Mystique: the probes read as on the 2164W

Date: 2026-10-19

Status: measured on A8U4I5 (Michael's Mystique, 051A revision 02), boot
508, build `eb27ccb4-dirty` (the Direct3D change,
[record](2026-10-19-mystique-direct3d.md)). No code changed for this: the
ICD and the Glide wrappers reach the engine through the render interface,
which the Z16 MGA table already serves. Phase 3 of
[matrox-mystique.md](../plans/matrox-mystique.md).

## Measured

Probes built from this tree and run one at a time. Reports in
[`docs/probe/a8u4i5-mystique-gl-glide-2026-10-19/`](../probe/a8u4i5-mystique-gl-glide-2026-10-19/).
Each was compared key by key with the 2164W's latest run in
`a8u4i5-mga2164w-render-interface-2026-10-18/`, ignoring build ids, times,
process ids and free VRAM. The VRAM figures differ as expected: 4 MiB here
against 8 MiB there.

| Probe | Mystique | Against the 2164W |
|---|---|---|
| V9XR3DP, 16 bpp | COMPLETE | identical but the renderer name ("Velocity9x Mystique") and the describe counter (boot 501) |
| V9XGLP (ICD), 16 bpp | PASS, renderer "Velocity9x Mystique" | identical to boot 501, except keys that run added later: the alpha keys equal the 2164W's `alpha-glp-16b.ini`, and `FarRgbaReplace` 0x009C8E63 equals its `alpha-glp-after.ini`, both taken after binary-alpha 4444 went to the engine as 1555 (bf287927) |
| V9XGLIDP (Glide 2), 16 bpp | FAIL, failures=1 | identical line for line to boot 501; the one failure is `BlendHalf`, which fails there by design (the Glide blend is drawn as the Direct3D stipple) |
| V9XGL3P (Glide 3) | SESSION, Step=done | identical to boot 493; this probe opens and closes a session and draws nothing |
| V9XR3DP, 32 bpp | COMPLETE | identical to boot 495 except the name and `DescribeTextureFormats` (0x0A here, 0x1A there). Boot 495 predates the end of 4444 on the 2164W; at 16 bpp both chips now read 0x0A |
| V9XGLP, 32 bpp | PASS, but renderer "GDI Generic" | as on the 2164W: the ICD offers no 32-bit pixel format ([record](2026-10-18-mga2164w-software-fallback.md)), so this run tested Microsoft's renderer, not ours |

## What this does not establish

- How much of each probe's drawing reached the engine on this chip. No
  V9XTRACE snapshot was taken around the runs.
- Games: Half-Life GL, the Glide titles on A8U4I5.
- OpenGL at 32 bpp, for the reason above.
- The 2164W on this build (the Direct3D record's open item).

Gates: none beyond the build scripts; no code changed.

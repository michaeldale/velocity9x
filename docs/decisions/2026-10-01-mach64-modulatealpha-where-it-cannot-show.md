# The Mach64 draws MODULATEALPHA where the alpha difference cannot show: Half-Life's refusals 74% to 1.7%

Date: 2026-10-01
Machine: Gateway SOLO2150, ATI Rage Mobility-M, 4 MiB, boots 81 and 82,
current ATI package (ABI 2026100102: DRV, VXD, HAL, SETP, ICD and
V9XTRACE from one build, hash-verified after the WININIT rename).
Workload: Half-Life 1.1.1.0, `timedemo mwd5`, three runs per session.
Evidence: [`../probe/hl1-gateway-2026-10-01/`](../probe/hl1-gateway-2026-10-01/)
Earlier record: [`2026-10-01-hl1-mwd5-on-the-gateway.md`](2026-10-01-hl1-mwd5-on-the-gateway.md)

## The change

The Mach64 policy refused every Direct3D `MODULATEALPHA` draw: no engine
mode gives A = At*Af, and its MODULATE gives A = At. The two draw the same
pixels whenever nothing reads the fragment's alpha (no alpha test, no
source-alpha blend factor) or every vertex's alpha is 255, so Af = 1 and
At*Af = At. The policy now accepts `MODULATEALPHA` as MODULATE in exactly
those cases and refuses it otherwise.

- `v9x_m64_draw_request.vertex_alpha_opaque`, set by the draw path from the
  batch's vertices (`v9x_d3d_mach64_vertices_opaque`).
- `V9X_R3D_DRAW.vertex_alpha_opaque`, set by the render interface from the
  request's vertices before the engine's `accepts()`, which has none: the
  ICD's GL_MODULATE draws reach the Mach64 through `accepts()`, and the
  first build of this change still refused all of them there.
- Host tests first, watched failing: `test_mach64_policy.c` (both sides of
  the rule, including a destination-only alpha factor) and
  `test_d3d_mach64_map.c` (the vertex scan).
- An instrument, ABI 2026100102: `M64PolicyNN` counts refusals by reason,
  and `M64TexOpRefused` is a bit per refused texture op.
  `m64_policy_last` alone had hidden the mix.

## Results

| Session | Batches drawn | Refused | Best fps |
|---|---|---|---|
| Direct3D, before (boot 80) | 204,048 | 580,351 (74%), last reason TEXTURE_OP | 13.07 |
| Direct3D, after (boot 81) | 954,414 | **16,976 (1.7%)** | **13.21** |
| OpenGL, current ICD, policy fix in the draw only (boot 81) | 135,592 on the engine | 916K ICD batches refused at `accepts()` | 7.02 |
| OpenGL, `accepts()` fix too (boot 82) | 706,123 | **0** | 7.00 |
| OpenGL before either (boot 80, older ICD) | - | ~1.9M triangles refused | 6.65 |

After the change, the Direct3D refusals left are: alpha test 13,436
(`M64Policy14`), texture address 3,183 (`M64Policy11`), texture shape 357
(`M64Policy08`), and no texture op (`M64TexOpRefused=0`). No FIFO or idle
timeouts and no resets in any session.

## What the speed says

Neither renderer got faster, though both now draw on the engine what they
were throwing away or drawing on the CPU. Direct3D draws 4.7 times the
batches at the same 13 fps; OpenGL moved its refused batches from the
software fallback to the engine at the same 7 fps. On this ~450 MHz CPU the
OpenGL frame (about 56 frames per ten seconds, ~179 ms) is the ICD's own
work: about 47 ms in `glVertex` (3.7 us a vertex), 31 ms in the batch sink,
and 53 ms in the interface draw into the HAL. That is the same CPU work the
netbook record measured, on a slower CPU.

## Not established

- That the pixels are right. Nobody watched; the equivalence is the
  argument above, and the host tests check the policy's side of it.
- Which Direct3D draws hit the remaining alpha-test and address
  refusals.

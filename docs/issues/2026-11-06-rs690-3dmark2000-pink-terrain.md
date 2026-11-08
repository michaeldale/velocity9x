# M-GB8KRE (RS690): 3DMark 2000's first game test draws some far terrain triangles salmon-red

Date: 2026-11-06. Machine: M-GB8KRE (ATI Radeon X1200 IGP, 1002:791F,
Win98 SE, DirectX 8.0), boot 84. Build: main's tree at e5f3a34f, the ati
package's eight files, clean. Michael: record it, not a blocker for
0.37.0.

## What was seen

The default benchmark ran at 1024x768x16 with V9XTRACE `-grab:40x10`
and scored 1610 (1781 at boot 80 without grabs). The ten grabs, all
from Game 1 (Helicopter), draw the terrain, trees, helicopter, rotor
and HUD. But in most of them, whole triangles of the distant hillsides
are tinted salmon-red, with hard straight edges
(`docs/probe/rs690-0.37.0-checks-2026-11-06/3dmark-grabs-sheet.jpg`,
`3dmark-grab2-top.png`). Sampled colours sit around D87060h-E8A090h.
The Rage 128's grabs of the same scene in the 0.36.0 release checks
fade those hills into pale grey haze, with no red
(`docs/probe/0.36.0-release-checks-2026-11-06/rage128/3dmark2000-grab/grabs-sheet.jpg`).

## What it is not, as far as read

- **Not a refusal.** The triangles are drawn. The run's snapshot
  (`3dmark-V9XSNA7.INI`) counts 1,558,423 draws and 176 refused, which
  is 6 colour key, 6 texture format and 164 texture filter. Some of
  those may be the probes that ran earlier in the boot. A refused draw
  is not drawn at all, so refusals do not explain a tinted one.
- **Probably not the fog colour.** The edges are hard, so this is per
  triangle, not a per-pixel fog blend. The fog constant's float24
  conversion and channel order were read and look right, and V9XD32's
  T20 fog (blue, full and half) passes.

## Not known

- Which draw state those triangles carry: texture format, stage
  operation, blend or specular. A V9XTRACE state capture of Game 1's
  terrain draws would say.
- Whether the boot 80 run (no grabs) showed it. No frame was captured
  then.

## Next

Capture the terrain draws' states and compare with what
`d3d_rs690_map.c` maps. The likeliest suspects are a texture format or
stage operation only Game 1 uses.

## 2026-11-07: the draws located, boots 93-98

Builds were tree-dirty on main after `16bce7db`. The DRV and HAL were
installed together by WININIT rename and `reset`, hashes checked.

Boot 93 installed the HAL alone. 3DMark then said "requires a DirectX 7
compatible 3D-accelerator", and V9XTRACE read `Error=abi-mismatch`: the
HAL's diagnostics block had grown and the old DRV's had not. The two must
come from one build when `V9X_D3D_DIAGNOSTICS` changes.

Evidence: `docs/probe/rs690-3dmark2000-salmon-2026-11-07/`.

### Hypotheses killed

- **A1R5G5B5 or A4R4G4B4 decode.** V9XRS3D TexFormat1555 and
  TexFormat4444 are exact at both depths. The 4444 corners read 0011FFh
  and 11EE00h, so red, green and blue sit right.
- **Fog colour.** The float24 conversion matches Mesa's
  `pack_float_24bit`. More to the point, no draw in the run has fog on
  (census, below), and FogTableBatches is 0.
- **A DX7 terrain layer faded by vertex alpha but drawn by the texel's
  alpha.** The core reduced COLOROP MODULATE with ALPHAOP SELECTARG of
  DIFFUSE to DX5 MODULATE, which takes the texel's alpha. Boot 94 added
  `texture.alpha_diffuse` and a counter. AlphaDiffuseDraws stayed 0 over
  1,614,514 draws, and the salmon stayed (`b94-alpha-diffuse-sheet.jpg`).
  The reason it never fired: Dp2StageStates, Dp2Stage0Approximated and
  Dp2Stage1States are all 0. On this driver the DirectX runtime folds
  3DMark's stage states into DX5 render states itself. The core change
  stays, because the gap is real for drivers that do get stage states.
- **Specular channel order.** V9XRS3D TexSpecularAdd puts a blue specular
  (000040h) in blue at both depths: 010040h at 32 bpp, 0008h at 16 bpp.

### The census (boot 95, `b95-census-snapshot.ini`)

A new V9XTRACE state census records every distinct Direct3D draw state
the engine is handed. It ran on every batch for boots 95-98. As
committed, it runs only when armed by `V9XTRACE -omit:2147483648`, which
can be added to an omit mask; unarmed, a batch pays one bit test. Game 1 used 16, with no overflow and no fog
anywhere. The key bits are in `d3d_state.h`, `v9x_d3d_state_census_key`.

### The bisect (boot 96, `b96-*`)

The RS690 drops classes of draw on request: V9XTRACE `-omit:<mask>`, the
shared omit mask, zero at boot. Game 1 ran once per mask, with six grabs
each. Salmon samples (`b96-salmon-counts.txt`):

| Mask | Dropped | Salmon |
|---|---|---|
| 0 | nothing | 8,604 |
| 1 | A4R4G4B4 textures | 13,985 |
| 2 | A8R8G8B8 textures | 10,421 |
| 8 | R5G6B5 textures | 8 |
| 16 | specular on | 1 |
| 32 | DESTCOLOR/ZERO | 9,119 |
| 64 | ONE/ONE | 1 |
| 128 | SRCALPHA/INVSRCALPHA | 17,296 |

Exactly one census state is R5G6B5-textured, specular and ONE/ONE: state
02. It is DECAL, CLAMP and trilinear on a 256x256 texture, added ONE/ONE
with specular on. It runs about 105,000 batches, as many as its partner
state 03: untextured, SRCALPHA/INVSRCALPHA, diffuse B0B0B0h. Without it
the hills lose their light pass along with the salmon
(`b96-mask0-vs-mask16.png`).

### What state 02 carries (boots 97-98, `b98-census-snapshot.ini`)

- The texel at its first vertex is 738Dh, a neutral grey.
- Its specular varies: the OR and the per-channel maximum are both
  00FFFFFFh.
- The vertex with the most red carries specular **00FF0000h**.

3DMark itself sends saturated red specular in this additive pass. The
RS690 adds it into the right channel, so the salmon is that specular.

### Not established

Whether that red is meant to be seen. Two explanations fit:

- A one-stage driver gets a different path from the Rage 128's two
  stages, and the runtime's DX5 folding loses a stage that would have
  used or replaced the specular.
- Direct3D does not add specular in a case where this HAL does.

Next: the same run on this machine under Direct3D's software renderer
(SYSTEM.INI `[Velocity9x] Direct3D=2`, about 47 minutes, recipe in
`docs/decisions/2026-11-06-rs690-dx8-3dmark2000.md`). Salmon there means
3DMark's one-stage path and no driver defect. None means the HAL is adding
specular where Direct3D does not.

## Status 2026-11-08 (audit)

The salmon was located on 2026-11-07 (8ad6bcab, [evidence](../probe/rs690-3dmark2000-salmon-2026-11-07/)). It is census state 02, an additive pass in which 3DMark itself sends red specular 00FF0000h. The run that would settle it, the same benchmark with `Direct3D=2`, has not been done. Parked.

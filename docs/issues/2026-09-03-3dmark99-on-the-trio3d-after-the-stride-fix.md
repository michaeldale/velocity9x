# 3DMark 99 on the Trio3D/2X after the texture-stride fix: what is still wrong

Date: 2026-09-03
Status: OPEN
Machine: A8U4I5, S3 Trio3D/2X (8A13), Windows 98, 5:5:5 desktop (`ColourLayout=555-auto`),
`Direct3DMode=hardware`. Driver pair at 060fb34 (binary identical to 88ea410).

## Where this stands

Three defects were removed from 3DMark 99 on this card today, each measured
before it was fixed:

- textures 128 and 256 texels across read as noise
  (`docs/decisions/2026-09-03-the-trio3d-reads-the-texture-stride.md`);
- textures near the top of VRAM refused for mip room they never used
  (commit 88ea410);
- alpha textures blended with MODULATE drawn as opaque black
  (`docs/decisions/2026-09-03-colour-key-and-blend-on-the-virge.md`).

The user's verdict on the run after the stride fix: "better but still issues".
The two photographs from that run, and the trace block read immediately after
it, are what this issue records. The machine was then shut down.

## The counters for that run

Counters reset at the boot that installed the pair, so these are the run's own:

```
D3dRenderPrimitiveCalls=56834   D3dTextureCreates=958   CountLock=4900  CountBlt=489
D3dMipChainChecks=619617        D3dMipChainGaps=255626
D3dTextureRefusedFormat=0  Shape=0  Other=0 (sysmem 0, no-cap 0, bounds 0)
D3dBlendSkipped=1 (the probe's)   D3dColorKeyDraws=1 (the probe's)
D3dTextureGreenDraws=5 (the probe's)   D3dTextureAlphaDraws=22843
EngineFifoTimeouts=0  EngineIdleTimeouts=0  EngineResets=0
D3dTextureLastOffset=0x00258000  Size=128  Caps=0x10405008  Texels=0x00000FFF
```

Every texture 3DMark bound was accepted by the sampler. Nothing was skipped
for blend, nothing was keyed, the engine never faulted. Whatever is wrong in
the pictures is drawn with the texture the application asked for, at the
address the application wrote.

## What the pictures show

Racing scene (first photograph):

1. **Sprites and HUD elements draw with an opaque black box around them** -
   the red engine-glow balls, the targeting reticle, the speedometer dial.
   These are alpha-textured quads; on the emulated ViRGE they draw with their
   background transparent. Same class as the black squares fixed on the
   emulator by taking alpha from the texel, so on this card the texel's alpha
   is either not being applied or not being read from the ARGB4444 texture.
   Note `D3dTextureLastTexels=0x00000FFF`: an ARGB4444 texel with alpha 0 and
   full colour, which is exactly what a sprite's transparent border looks
   like - the data is right.
2. **Ground and roadside textures are noise**, streaked along the direction
   of travel - grey/white with saturated speckle. Distinct from the pre-fix
   scramble (which had halves swapped); this looks like a texture read at the
   wrong mip level or with linear filtering across garbage.
3. **The speedometer and lap counter panels are drawn in solid magenta** with
   their detail on top - a colour-keyed or alpha background showing through as
   its key colour.

Corridor scene (second photograph):

4. Textured walls are correct, and were not before the stride fix.
5. **Sprites again have black boxes** (the ship at left, the lights).
6. **Large flat white and black triangles** hang from the ceiling - untextured
   or wrongly textured polygons that the emulator does not show.
7. **The health-monitor and ammo panels at bottom left are magenta**, as in 3.

## Hypotheses, in the order to test them

**A. The Trio3D does not take alpha from an ARGB4444 texel the way the
ViRGE/DX does.** Fits 1, 3, 5, 7: every black box and magenta panel is an
alpha texture. The probe's `Tex4444PixelOk=1` on the card only checks colour of
an opaque texel; there is no rung that blends an ARGB4444 texel with alpha 0.
Test: a rung with a 4444 texture half alpha-F, half alpha-0, ALPHABLENDENABLE
with SRCALPHA/INVSRCALPHA, MODULATE, drawn over a known fill; read both halves.
Then the same with ARGB1555. Run on emulator and card.

The 1555 colour-key rung (`ColorKeyOk=1`) does pass on the card, and it draws
with texel alpha 0 - so texel alpha is honoured for 1555 at least in the key
path, where the texel's alpha bit is written by the driver. That narrows A to
4444, or to an interaction with MODULATE.

**B. The mip-level record for this card is wrong and now needs re-measuring.**
`docs/issues/2026-09-03-trio3d-alpha-and-mip-differ-from-virge-dx.md` was
written with the texture stride wrong on every mipmapped draw. Fits 2 if the
card selects a level below the top on a chain with gaps (255,626 of 619,617
chain checks here had gaps, so the engine asked for level 0 only - and if the
card reads a lower level anyway, it reads whatever DirectDraw put after the
top level). Test: the two-level probe texture with level 1 deliberately
elsewhere and the memory after level 0 filled a known colour; draw with plain
LINEAR filtering; see which colour comes back on the card.

**C. Something about flat triangles (6).** No counter speaks to this. They
appear only on the card. Could be B (a texture read as uniform white/black
from garbage), could be depth. Take the mip test first.

## What was not the problem

- Refused textures: zero of any kind in this run.
- Blend pairs: 3DMark used none the S3D lacks.
- Colour keys: 3DMark set none.
- Engine health: no faults.
- Texture addressing at 128 and 256: fixed and measured (`Tex128HalvesOk=1`,
  `Tex256HalvesOk=1` on the card).

## Files

- `src/display32/d3d/d3d_virge.c` - `v9x_d3d_virge_alpha_bits`, the texel-alpha
  rule; `v9x_d3d_texture_info`, the sampler's rules; the stride write.
- `tools/diag/ddraw_probe_win32.c` - the `Tex*HalvesOk`, `ColorKeyOk`,
  `Tex4444PixelOk` rungs; the alpha-4444 rung proposed above does not exist yet.
- Trace snapshot: scratchpad `trio3d_3dm3.ini` from this session; not in the tree.

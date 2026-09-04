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

## Second run, 2026-09-04: a score, a screenshot route, and which items survive

Driver pair at 4e228fe (the counters escape, the alpha-curve probe rung and the
learning 3D-done wait), A8U4I5 boot 26, 800x600x16, `Direct3DMode=hardware`.
The benchmark completed: **335 3DMarks, 13,186 CPU 3DMarks** - the first score
this card has produced on this driver.

Screenshots are possible after all. `v9xctl screenshot` returns the real
primary while 3DMark is in full-screen exclusive mode, so the photographs the
first run needed are not needed again; frames are in `docs\images`.

### Counters for the run

```
D3dRenderPrimitiveCalls=58619  D3dTextureCreates=27290  CountFlip=13193
D3dTextureRefusedFormat=0  Shape=0  Other=0 (sysmem 0, no-cap 0, bounds 0)
D3dBlendSkipped=1 (the probe's)  D3dColorKeyDraws=1 (the probe's)
D3dTextureAlphaDraws=23355
EngineFifoTimeouts=0  EngineIdleTimeouts=0  EngineResets=0
D3dDoneSeen=0  D3dDoneMissing=64  D3dDoneSkipped=483491
D3dMipChainChecks=481514  D3dMipChainGaps=427851
D3dTextureLastTexels=0x00000FFF
```

Again not one refusal, not one engine fault, and no blend pair the S3D lacks.
`D3dDoneSkipped=483,491` is the learning wait's payoff on one benchmark run:
that many full 4,096-read spins not taken
(`../decisions/2026-09-04-the-idle-wait-learns-the-done-bit.md`).

### The list, re-read

- **1, 5 - sprites and HUD in opaque black boxes: still there**, and now
  explained. The alpha curve says a blended texel of alpha 0 writes **black**
  on this part where it should keep the destination
  (`AlphaCurve_0_Raw=0` against the emulator's 31744,
  `../decisions/2026-09-04-what-the-trio3d-blend-does-with-its-operands.md`).
  A sprite's transparent border is exactly that draw. The black box is the
  alpha defect seen in an application; it needs no separate investigation.
- **2 - ground and roadside textures as noise: still there.** The race scene's
  track is white and grey chevrons. This is *not* the alpha defect and not the
  stride: the probe now draws 64-, 128- and 256-texel textures correctly under
  every filter and layout, and the corridor's walls, floor and ceiling in the
  same run are right. Hypothesis B in this issue is answered separately - mip
  selection matches the emulator now - so item 2 is the open one, and it is
  what to instrument next.
- **3, 7 - panels in solid magenta: still there** (the ammo panel, bottom left
  of the corridor frame).
- **4 - textured walls correct: confirmed**, and the synthetic texture-rendering
  test draws its tiled, lit grid correctly.
- **6 - large flat triangles: present as one large flat green panel** on the
  corridor's left wall, which is the same open item the stride decision left
  ("flat green panels ... with zero refusals and zero probe-green draws").
  `D3dTextureGreenDraws=7` for the whole run: the probe's own, so still not the
  driver's green fill.

Frames: `../images/3dmark99-trio3d-2026-09-04-race.png`,
`../images/3dmark99-trio3d-2026-09-04-corridor.png`,
`../images/3dmark99-trio3d-2026-09-04-score.png`.

### What is left here

Two things, and neither is alpha: the noise ground (2) and the flat green panel
(6). Both draw with textures the sampler handles correctly elsewhere in the
same frame, with nothing refused and no fault, so the next move is an
instrument rather than a hypothesis - what those particular draws bind and with
what state.

## Third run, 2026-09-04: 640x480, and the ground was never a sampler fault

Full record in
[`../decisions/2026-09-04-four-megabytes-is-the-resolution-limit.md`](../decisions/2026-09-04-four-megabytes-is-the-resolution-limit.md).

**313 3DMarks at 800x600, 475 at 640x480**, same boot, same driver. The card
has 4 MiB and 3DMark's own settings page reports 3,750 KB of it consumed by a
triple frame buffer and a 16-bit depth buffer at 800x600, against 2,400 KB at
640x480.

Item 2 - the race scene's ground as chevron noise - **is resolved, and it was
not a sampler defect**. Mip-filtered draws go from 2,861 to 418,390 between the
two resolutions: under 800x600's memory pressure DirectDraw cannot lay a mip
chain out contiguously often enough, the engine's contiguity guard correctly
falls back to a plain LINEAR filter, and a large receding ground plane sampled
without mipmaps aliases into exactly those moving chevrons. At 640x480 the
ground is smooth. Nothing was refused in either run.

Items 1, 3, 5, 7 - the black boxes and the magenta panels - are **unchanged at
both resolutions**, and they are now the interesting ones: on this boot the
probe says the card's alpha blend is correct in every rung it has. So the black
box is not "the part cannot blend". 3DMark's sprite draws differ from the
probe's alpha cells in three ways the probe does not cross - `Z:LESS` with no
depth write against depth-off, LINEAR against NEAREST, and UNLIT against
LIT+MODULATE. That is the rung to write next.

Frames: `../images/3dmark99-trio3d-2026-09-04-800x600-race.png`,
`../images/3dmark99-trio3d-2026-09-04-640x480-race.png`,
`../images/3dmark99-trio3d-2026-09-04-640x480-corridor.png`,
`../images/3dmark99-trio3d-2026-09-04-640x480-score.png`.

# GMA 950, 3DMark 2001 SE: Dragothic's sky draws white

Date: 2026-11-07. Seen by Michael on the netbook's panel (W9QI2BE2,
945GSE / GMA 950), build `089a5e70`, boot 284, 3DMark 2001 SE default
benchmark at 640x480, 16-bit colour and 16-bit textures. Status:
**parked 2026-11-07** at Michael's word, for a later version. 3DMark
binds no texture for the white geometry (below), and the cause is not
found. **2026-11-12: the sky draws with the texture pool**, and the
dragon's white neck and horns were dropped texture stages, now drawn
(sections at the end). Untextured draws remain.

If it proves to be texture memory, the fix belongs to
[`../plans/intel-gen3-system-memory-textures.md`](../plans/intel-gen3-system-memory-textures.md)
(a 32 MiB texture pool in system memory through the GTT, for this
part's ~6 MB heap). If it is not, it joins the HD 4600's parked Nature
defect. The first test under Next decides which.

## What is known

The counters of the run (`../probe/gen3-depth-edge-2026-11-07/watched-pre.INI`
and `watched-post.INI`, whole 3DMark runs, not Dragothic alone):
- No texture refused: `D3dTextureCreates` 68,601 = `D3dTextureDestroys`,
  every `D3dTextureRefused*` 0.
- `MipTreeDeclined` rose by 7,486 in the run, last reason 4 (no room for
  the driver's own placement). A declined chain is left to DirectDraw's
  heap, where Gen3 can sample only its top level (`d3d_i9xx.c`, MIP
  TREES). That leaves a texture without its smaller levels, not a white
  surface.
- 0 batches refused.

So the sky is not the lost-texture or refused-batch pattern those
counters can show.

## Candidates, none tested

- The sky's material uses a second texture stage, which Gen3's Direct3D
  does not offer (`texture_stages` 0), and the remaining stage draws
  white.
- A stage operation or blend the engine expresses differently.
- A refused draw falling to the CPU fallback, which cannot sample a
  surface texture in Gen3's mip layout (`v9x_r3d_fallback` returns
  UNSUPPORTED for those).

It looks like the HD 4600's open Nature defect (distant terrain and
mountains white, 2026-10-30-haswell-ddi8-ps11-record.md), but that is a
resemblance, not a finding.

## 2026-11-07: measured - nothing is bound

Dragothic alone (low and high detail) on boots 285 and 286.
- Boot 285 ran `efd2fa90`. Boot 286 ran `39893580-dirty`, which adds the
  state census to indexed draws, which Gen3 takes whole and which the
  census had missed.
- Census and frame grab armed (`V9XTRACE -omit:2147483648 -grab:60x10`).
- Evidence:
  [`../probe/gen3-dragothic-sky-2026-11-07/`](../probe/gen3-dragothic-sky-2026-11-07/)
  (`b286-post.INI`, `boot285-V9XGRAB1-low-detail.png`, `boot285-V9XGRAB6-high-detail.png`).

**The frames** (`V9XGRAB1` on boot 285, low detail; `V9XGRAB6`, high
detail) show the sky flat white around the tower, and a white wedge in
a corner of the high-detail frame.

**The draws.** `DrawsNoHandle` is 1,894 and `DrawsHandleUnresolved` 0.
So the white geometry reaches the driver with **no texture bound** at
all, not with a texture the driver lost. Census slot 8 (`0x28700900`,
194 batches) is such a state:
- untextured, depth test and write on, no blend;
- diffuse `FFFFFFFF`;
- MIPLINEAR and LINEAR filters and wrap addressing set, as if a texture
  were meant.

Slots 3 and 7 are untextured multiply passes (DESTCOLOR x ZERO) with the
same leftover filters. `Dp2Stage1States` is 0: nothing is set on a
second stage either.

This is the HD 4600's parked Nature defect
(`docs/plans/haswell-ddi8-pixel-shaders.md`, Parked): 3DMark's renderer
binds NULL because the material's map has no device texture, and no
error reaches the driver. That investigation ruled out DXT, texture
size, formats, ops, address modes, projection, a depth-only pass, a
94 MB texture budget and render-to-texture.

What is particular to the GMA 950: its DirectDraw heap is about 6 MB,
and each 3DMark run declines thousands of mip chains for room
(`MipTreeDeclined` +10,288 on these boots). A chain the HAL declines
goes to DirectDraw's own heap. If that allocation fails, the error goes
to the application without passing anything this driver counts. So a
material whose texture never fit is a live hypothesis here, which it
could not be on the HD 4600. It is not measured.

## Next

- **Test the heap hypothesis.** Count texture surface creations the HAL
  declines, with the free heap at that moment. Then see whether any
  are declined when there is too little room for DirectDraw to place
  them.
- **Compare the same run on a card with a large heap** running
  Velocity9x (an Evergreen or the HD 4600). If its Dragothic sky is
  white too, the heap is not it, and this joins the Nature defect.
- A reference frame from real DirectX 8 hardware would settle what the
  sky should be.

## 2026-11-12: with the texture pool, the sky draws

Dragothic alone (low and high detail), boot 319, build `3c7d7ab6`: the
32 MiB texture pool in system RAM
(`../decisions/2026-11-12-intel-gen3-texture-pool-phase3.md`). Same
settings as before but 32-bit textures; census and grab armed as on
boot 286 (`V9XTRACE -omit:2147483648 -grab:60x10`). Evidence:
[`../probe/gen3-dragothic-sky-2026-11-12/`](../probe/gen3-dragothic-sky-2026-11-12/).

- **The sky is textured.** `boot319-V9XGRAB2-low-detail.png` is the
  moment of boot 285's `V9XGRAB1` (12:39 against 12:46 on the test's
  clock): the sky behind the castle and through the rock arch, flat white
  then, is a dark storm-cloud texture now. `V9XGRAB1` shows the same
  over the town.
- **Nothing was declined for room.** `MipTreeDeclined` stayed at 118
  across the run while 802 mip trees were placed; it rose by 7,486 in the
  boot-284 run. That fits the heap hypothesis above for the sky.
- **Untextured draws remain.** `DrawsNoHandle` rose by 2,715
  (`DrawsHandleUnresolved` 0). In `boot319-V9XGRAB3-low-detail-dragon.png`
  the dragon's neck and head plates are smooth shaded white with no
  scales, beside a textured scaled patch. The bone-white horns look as
  in boot 285's high-detail frame and are probably meant. No earlier
  frame of this moment exists, so whether the white neck is new is not
  known.

So the memory half of this issue is answered; what is left is the
no-texture-bound pattern the HD 4600's Nature defect shows, which no
amount of heap fixes. Next: the census slot of the white neck's draws,
and the same run on the HD 4600 for comparison.

## 2026-11-12: the dragon - stages Gen3 dropped

The white neck and the white horns were not unbound textures. They were
texture stages the driver did not draw. A logging `D3D8.DLL` proxy beside
3DMark (scratchpad tool, not in the tree; 3DMark's shader code stays out
of the repository) recorded each draw's vertex shader and its stage
states. A skip experiment (the proxy drops the draws of one shader)
named the dragon's: hiding vertex shader 3 removes the whole dragon.
Hiding the projected reflection-map key `0x252` leaves it, so that key
is not the horns.

The dragon's draws are three stages, under a vertex shader that writes
two coordinate sets and two point lights into `oD0`:
- stage 0: SELECTARG1 TEXTURE;
- stage 1: MODULATE TEXTURE CURRENT;
- stage 2: MODULATE CURRENT DIFFUSE, untextured.

6,388 draws used this in one run (proxy, boot 322).

**Neck and scales.** `d3d_i9xx.c` reported `texture_stages` 0, so stage
1, the scale texture, was dropped. Now it reports 2 and gives DDI 6 to
every program. Stage 1 runs on the second unit: `TwoUnitBatches` 283,696
and `TwoUnitDeclined` 0 in the run. The two-unit program gained ADD,
which the T12 two-stage test exercises once two stages are offered
(`i9xx_multitex.c`, `test_i9xx_3d.c`). TEXTURETRANSFORMFLAGS PROJECTED
is now applied to both stages' coordinates (`v9x_dp2_fvf_project`). That
is for key `0x252`; its effect on the frame was not isolated.

**Horns.** Stage 2 carries the lighting, and the core dropped every
stage past 1, so the dragon drew unlit. The horns, whose textures are
pale, drew flat white. Gen3 has no third unit. But t0 · t1 · d is the
same product as stage 0 MODULATE TEXTURE DIFFUSE under the same stage 1.
`v9x_d3d_state_stage2_fold` (`d3d_state.c`, tested in
`test_d3d_state.c`) makes that rewrite, and only when every step is a
product and stage 2's alpha passes through. Any other stage 2 is still
dropped.

The two frames below are the same moment (20:7x on the test's clock):
- `two-stage-V9XGRAB4-horns-unlit.png`, boot 322: two stages without the
  fold. The horns are flat white, and the back, spikes and peasants are
  full-bright.
- `stage2-fold-V9XGRAB4-horns-lit.png`, boot 323: with the fold. The
  horns shade from bone to blue-grey under the two lights, and the
  dragon and the peasants are lit.

The earlier note that the white horns are "probably meant" is wrong.

Not yet run on these builds: the release probe set on the netbook,
Half-Life (DDI 6 now offered to DX6/7 programs), and the HD 4600
comparison. The no-texture-bound draws (`DrawsNoHandle`) are separate,
and they remain.

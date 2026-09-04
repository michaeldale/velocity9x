# Four megabytes is what picks the resolution, and at 800x600 mipmapping is off

Date: 2026-09-04
Status: measured on A8U4I5, two 3DMark 99 runs, same boot, same driver, only
the benchmark's render resolution differing

## The suggestion, and why it was right

The card has 4 MiB. At 800x600x16 with a triple frame buffer and a 16-bit
depth buffer, 3DMark 99's own Display Settings page reports **3,750 KB of
4,096 KB used** before a single texture is allocated. At 640x480 the same page
reports **2,400 KB**. That is 1,350 KB back.

The consequence is not only frame rate.

## Measured

Both runs on boot 32, driver at e317614, the same probe-verified good state:

```
                                800x600            640x480
3DMarks                             313                475     +52%
CPU 3DMarks                      13,554             12,798
Render primitive calls           54,954         over 1.1M
Mip-filtered draws                2,861            418,390     x146
Texture refusals (any kind)           0                  0
Census slots / overflow          31 / 0           32 / 278
```

**At 800x600 this card is barely mipmapping at all.** 2,861 of 54,954 draws
carry a mip filter; at 640x480 it is 418,390. The mechanism is the engine's own
contiguity guard: `v9x_d3d_texture_info` sets `texture_mipmapped` only when
`v9x_d3d_mip_chain_contiguous` succeeds, and under 800x600's memory pressure
DirectDraw cannot lay the levels of a chain end to end often enough, so the
driver correctly falls back to a plain LINEAR filter. Nothing is refused and no
counter complains - the textures are all accepted, they are just sampled
without their chains.

The census makes the same point through the texture addresses. At 800x600 every
mip-filtered word's last texture sits at 0x003FEB00 or 0x003FA080 - the final
24 KB of the card. At 640x480 they are at 0x00294F80 and 0x002AC780, in the
middle of it.

## What that explains

**The race scene's ground.** At 800x600 it is the white and grey chevron noise
this issue has carried since 2026-09-03
(`docs/issues/2026-09-03-3dmark99-on-the-trio3d-after-the-stride-fix.md`, item
2). At 640x480 it is smooth. A large receding ground plane sampled without
mipmaps is exactly what aliases into moving chevrons, and it was never a
sampler defect.

That also retires the guess in `2026-09-04-the-command-word-census.md` that the
noise lived *among* the 3,775 mip-filtered draws. It lived among the draws that
should have been mip-filtered and were not. The census was right that those
draws were the place to look and wrong about which way round it was.

## What it does not explain

Unchanged at both resolutions, so not about memory:

- **Sprites and HUD still draw in opaque black boxes**, at 640x480 as at
  800x600. This is the one that matters, because on this boot the probe says
  the card's alpha blend is correct in every rung it has - three operand
  curves, both format rungs, all eighteen matrix `alpha` cells
  (`2026-09-04-the-trilinear-two-pass-and-a-retraction.md`). So the black box
  is *not* simply "the part cannot blend", which is what this issue has assumed
  since it was filed. 3DMark's sprite draws differ from the probe's alpha cells
  in three ways the probe does not currently cross: they are `Z:LESS` with no
  depth write where the probe's are depth-off, they are LINEAR where the
  probe's are NEAREST, and half of them are UNLIT where the probe's are
  LIT+MODULATE. That is the next rung.
- **Magenta panels**, and the flat green panel on the corridor wall.

## Two other things this run found

- **The census table is too small for this workload.** `CensusOverflow=278` at
  640x480 with all 32 slots used. The overflow is counted, so no reading is
  silently wrong, but the tail is lost.
- **The trilinear degrade is partial, by construction.** 1,807 draws at 800x600
  still emit the chip's native `LINMIPLIN`, because `trilinear_degrade` is set
  only where the two-pass form would have been used - inside the branch that
  needs a non-zero LOD fraction. A draw whose LOD lands exactly on a level
  never took that path and never will. Whether the native encoding is right on
  this part is untested: every `MipTri` step in the ladder is deliberately half
  a level, so all three took the degraded path.

## What to do with it

Nothing in the driver, yet. What this changes is how the card is run and how
its results are read: **640x480 is the resolution for a 4 MiB Trio3D**, and any
3DMark or Final Reality number taken at 800x600 on this card was taken with
mipmapping mostly disabled and should be labelled as such.

## Gates

None needed; no code changed. The evidence is the two censuses in
`docs/probe/references/` and the frames in `docs/images/`.

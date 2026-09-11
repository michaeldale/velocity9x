# A texture with no pixel format is in the display's format

Date: 2026-09-11. Measured on the 86Box Trio64 guest, agent 9871, boots 349
and 351, with `Direct3D=2` so every Direct3D draw is served by the CPU
rasterizer.

Final Reality 1.01's Robots scene drew with no textures at all through this
driver and fully textured through Microsoft's software rasterizer, on the
same guest in the same boot. The cause and the evidence are in
[the issue](../issues/2026-09-11-every-final-reality-texture-is-refused-for-having-no-pixel-format.md);
this is the fix and what it measured.

## The rule that was wrong

Both texture classifiers refused a surface that carries no
`DDRAWISURF_HASPIXELFORMAT`:

```c
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) == 0ul) {
        return 0;
    }
```

The flag does not mean the format is unknown. It means the surface does not
*differ* from the primary - DirectDraw allocates `ddpfSurface` only in the
differing case, which is why the field must not be read without it. The
format of such a surface is the display's, and the display is 5:6:5 or 5:5:5,
both of which the classifier accepts when it is given them.

`v9x_d3d_target_layout` in the core has resolved the same flag correctly
since 2026-09-02, falling back to `v9x_hal->info.vmiData.ddpfDisplay`. The
texture functions are the siblings that never got the fallback. They have it
now, in `d3d_soft.c` behind a `v9x_d3d_soft_texture_pixel` helper the refusal
detail shares, and in `d3d_virge.c` inline.

**The hardware path is changed too, and deliberately.** Its old comment
argued the refusal was right because "the primary's format is RGB565 here and
not a format this engine can sample". That is true of a 5:6:5 desktop and
false on the 5:5:5 one this driver selects for hardware Direct3D, where the
display format is exactly what the S3D texture unit wants. Handing the masks
the display format settles it either way: a 5:6:5 display still refuses,
because 565 is not in the ViRGE's list, and now records the mask rather than
a sentinel meaning only "no format was read". Not measured on a ViRGE guest.

## What it measured

Final Reality, Robots only, one run, every other test cleared:

| | before, boot 349 | after, boot 351 |
|---|---|---|
| `D3dTextureRefusedFormat` | **39793** | **0** |
| `D3dTextureRefusedLast` | 0xFFFFFFFF | 0 |
| textures created | 382 | 466 |
| `D3dRenderPrimitiveCalls` | - | 8753 |
| the picture | white and grey Gouraud | textured |

Every other refusal counter is zero on both sides, so the format arm was the
whole of it.

`robots-valley-before-vs-after.png` is the same camera position in the same
scene through this driver either side of the change, and
`robots-valley-fixed-vs-software.png` is the fixed driver against Microsoft's
rasterizer: rock, ground, sky and robot all present and in the same colours.

The two are not pixel-identical and should not be. Ours is brighter in the
distance, because Final Reality's own capability page greys out depth fog for
our device and lights it for Microsoft's - that difference was there before
this change and is untouched by it.

## The probe rung is reported, not judged

The issue observes that no probe cell creates a texture without naming a
format, which is why none of them could see this. One now does -
`DisplayFmtTex*` - and it reports what DirectDraw chose for the surface
(`Caps=0x10005000`, local video memory; `Pitch=128`; `PfBits=16`;
`PfRed=0xF800`, which is the display's 5:6:5) and what came back.

**It has no verdict key, and that is deliberate.** With the fix in place and
Final Reality rendering the same kind of texture correctly, the cell's own
draw writes nothing at all: both sampled pixels read back the fill, while
`BeginScene` and `DrawPrimitive` return zero and every refusal counter stays
at zero. Its neighbour, the system-memory cell, draws correctly in the same
run with the same geometry against the same target; swapping `COPY` for
`MODULATE` changed nothing; the texture itself reads back green before the
draw, so it is filled.

The application is the stronger evidence, so the suspicion is on the cell
rather than the driver - but that is a suspicion, and an `Ok` key would
currently assert that the driver fails a case it demonstrably passes. The
cell records its observations and claims nothing until the disagreement is
explained. One candidate not yet tested: the cell runs immediately after
another cell that released a texture, so a recycled handle is worth ruling
out by moving it earlier.

## What this does not cover

**Nothing physical, and no ViRGE.** Emulator only, software engine only. The
hardware-path change is read from the source and has not been run on a ViRGE
guest, where the 5:5:5 case it is meant to help actually arises.

**No speed number.** The textured scene is visibly slower than the untextured
one was, which is what sampling a texture costs; nothing here measures it,
and Final Reality's own figure was captured for only one of the runs.

**No claim the two rasterizers now agree.** They agree about textures. FR
still grants Microsoft's device mip-mapping and depth fog and ours neither.

## Gates

`run-checks.ps1` green, including the rasterizer's frozen pixel table, which
this does not touch - the change is which `DDPIXELFORMAT` the classifier
reads, not how a texel is decoded. Verified on `Win98SE-Trio64` with
`Direct3D=2`. `build-host-msvc.ps1` cannot run on this host.

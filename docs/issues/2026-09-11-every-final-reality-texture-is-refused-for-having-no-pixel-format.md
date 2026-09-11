# Every Final Reality texture is refused for having no pixel format

Found 2026-09-11 on the 86Box Trio64 guest, agent 9871, boot 349, with
`Direct3D=2` so every draw is served by the CPU rasterizer.

> **Fixed the same day**, in both engines, by resolving the absent flag to
> the display's format instead of refusing:
> [a texture with no pixel format is in the display's
> format](../decisions/2026-09-11-a-texture-with-no-pixel-format-is-in-the-displays-format.md).
> Boot 351 runs the same scene textured with `D3dTextureRefusedFormat=0`
> against this boot's 39793. Two things below are **not** closed: the ViRGE
> path is changed but unmeasured, and the probe rung this issue asks for
> exists but disagrees with the application and carries no verdict yet.

Final Reality 1.01's Robots scene, run once through each rendering platform
one after the other in the same boot:

| | Velocity9x HAL | Microsoft Direct3D Software |
|---|---|---|
| geometry | correct | correct |
| textures | **none** | all present |

`docs/probe/final-reality-robots-2026-09-11/robots-valley-hal-vs-software.png`
is a matched pair - the same camera position in the same scene - and
`robots-closeup-hal-vs-software.png` is the robot itself. Ours draws white
and grey Gouraud where the reference draws rock, metal grating and a painted
robot. Nothing failed: FR enumerated our device as `Direct3D On-board
Accelerator`, created every context and every texture, ran the scene and
scored it.

## The counter says why

`V9XTRACE` after both runs
(`v9xsnap-after-both-runs.ini`):

```
D3dTextureCreates         382
D3dTextureDestroys        370
D3dTextureRefusedFormat   39793
D3dTextureRefusedShape    0
D3dTextureRefusedOther    0
D3dTextureRefusedLast     0xFFFFFFFF
D3dTextureRefusedCaps     0x14005000
```

Every refusal took the format arm, and `0xFFFFFFFF` is not a pixel mask - it
is the sentinel `v9x_d3d_soft_texture_setup` writes when the surface carries
**no** `DDRAWISURF_HASPIXELFORMAT` flag ([d3d_soft.c:348](../../src/display32/d3d/d3d_soft.c)).
This is the trap those counters were added for: a refused texture draws as
untextured Gouraud in the vertex colour, every call returns success, and the
picture is merely wrong.

## The rule that refuses is the wrong rule

A surface without `DDRAWISURF_HASPIXELFORMAT` is not a surface with an
unknown format. It is a surface **in the display's format** - DirectDraw
allocates `ddpfSurface` only when the surface differs from the primary. The
texture path reads that as "no format" and refuses:

```c
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) == 0ul) {
        return 0;                       /* d3d_soft.c:197 */
    }
```

The render-target path in the same driver already does the right thing with
the same flag, and its comment records that this exact mistake was found and
fixed there on 2026-09-02:

```c
        const V9X_DDPIXELFORMAT *format =
            (target->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul
                ? &global->ddpfSurface
                : &v9x_hal->info.vmiData.ddpfDisplay;   /* d3d_core.c:577 */
```

The texture function is the sibling that was never given the fallback. The
guest's display format during the run is 5:6:5 - `D3DExpectGreen` reads 2016
on this guest - which `v9x_d3d_soft_texture_format` accepts when it is asked.
It is never asked.

**The hardware path has it too.** `v9x_d3d_texture_format` in
[d3d_virge.c:117](../../src/display32/d3d/d3d_virge.c) refuses on the same
flag with the same `0xfffffffful` detail. On a ViRGE the driver selects a
matching 5:5:5 mode, which the S3D texture unit can sample, so a
display-format texture would be sampleable there too and is being dropped for
the same non-reason. Not yet measured on a ViRGE guest - that is the first
thing to do.

## Why the probe never caught it

`V9XDDP`'s texture cells create their surfaces with an explicit
`DDPIXELFORMAT`, because the question they were written to ask is which
formats the driver accepts - `TexFormatCount=3`, `TexFormat565=1`,
`TexFormat1555=1`. A texture created that way carries the flag and takes the
arm that works. An application that simply asks for a texture and lets the
runtime give it the display's format takes the arm that refuses, and no probe
rung does that.

A rung that creates a texture **without** naming a format belongs in the
probe whether or not the fix lands first; it is the case every real
application hits.

## Not established

- **Whether all 39,793 refusals are this one cause.** The counter keeps only
  the last detail. It is `0xFFFFFFFF`, and no other refusal counter moved, so
  every refusal took the format arm - but a run with a per-reason breakdown,
  or a fix that makes the count go to zero, is what would settle it.
- **Anything about the ViRGE.** Read from the source, not measured.
- **That the fix makes the two pictures match.** Textures are the visible
  difference; FR's own option list also greys out mip-mapping and depth fog
  for our device and enables them for Microsoft's, so some difference should
  be expected to survive.

## Reproduce

Install FR 1.01, Advanced Options, `Clear all tests`, tick `Robots`, untick
`Run all tests 5 times`, run once per rendering platform, then `V9XTRACE`.
Capture the frames host-side with `PrintWindow(hwnd, hdc, 2)` on the 86Box
window - the guest agent's screenshot reads the GDI primary and is black
while DirectDraw is flipping.

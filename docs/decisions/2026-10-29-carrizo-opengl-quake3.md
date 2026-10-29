# Carrizo runs Quake 3 through the OpenGL ICD in hardware, once its Z16 rows were whole 64 bytes

Date: 2026-10-29

Status: **measured, one machine.** M-XP7053's Radeon R7E (`1002:9874`),
boots 37 and 38. Follows
[the zero rows record](2026-10-29-carrizo-ddp-zero-rows.md). Captures are
in [`../probe/m-xp7053-carrizo-opengl-2026-10-29/`](../probe/m-xp7053-carrizo-opengl-2026-10-29/).

## The change, after Cedar's

[Cedar's](2026-10-27-cedar-opengl-quake3.md) two gaps were here too:
- `d3d_vi_map.c` refused every explicit draw, which is how the ICD and
  Glide reach the engine. It now takes their scissor and write mask:
  - the scissor must be a non-empty rectangle inside the target, or the
    draw goes to the fallback;
  - `v9x_vi_tl_draw` gains `scissor_*`, which write PA_SC_GENERIC_SCISSOR
    again after the common state's whole-target one;
  - the write mask clears CB_TARGET_MASK's B, G and R bits (0, 1, 2 in
    memory order). No channel written is `target_mask_none`, because a
    zero mask means all.
- `d3d_core.c` published no surface textures for this engine to the
  render interface. It now does, as for Cedar: powers of two, 1 to 2048,
  in all three formats.

CPU levels and a second texture unit still go to the fallback.

The ICD was deployed with the HAL. The vbe install's `V9XGL.DLL` was the
0.32.0 package's, and Cedar's boot 32 showed what an old ICD does.

## Boot 37: every batch with a depth buffer failed

V9XGLP made a context ("Velocity9x Radeon R7E", GL 1.1). Its clears and
scissored clears read back right, and its T&L scene filled the window.
The depth-tested scenes did not:
- A full-window quad filled half of the middle row.
- `V9XGL.LOG`'s failure dumps read `draw result=1`, which is
  `V9X_R3D_RESULT_INVALID`.
- The snapshot read `D3dDepthReject=pitch`.

The ICD's `paths ... hw=116` counts batches sent, not batches drawn.

The cause is placement. `d3d_vi.c` gave a Z16 rows rounded to 16 bytes,
the DB's 8-pixel tile. The core binds a depth buffer only when its pitch
is a multiple of the engine's target row alignment, 64 bytes here
(`v9x_d3d_set_target`, shared with Direct3D). For the probe's 400-pixel
drawable that is 800 bytes, which fails, so the render interface refused
the bind and the batch with it. 3DMark 2000 at 1024 and Cedar's Quake 3
at 640 never met this, because 2048 and 1280 are multiples of 64.

A Z16 now gets rows of whole 64 bytes, still whole 8-pixel tiles.

## Boot 38

**V9XGLP:** no batch fails.
- The full-window quad fills every eighth of the row.
- Textures, mips, arrays, the scissored geometry, fog and blending read
  as on the Ironlake (`frame-grab-ironlake-2026-10-27/busy-V9XGLP.INI`),
  allowing for this desktop's 16 bpp against its 32.
- Four rows differ and are not explained:
  - the geometry scene's left half (`GeometryLeftPixel` and
    `ReadGeometryLeft` black, where green is expected);
  - `AlphaSolid` (blue, where green is expected);
  - `Small2x2` (`0x08E300`, where green is expected);
  - the T&L scene, which drew 26,434 pixels against the Ironlake's
    28,196, with one pixel off by 222.

**Quake 3 demo** (`C:\games\Q3Ademo`, pushed from the Haswell's copy over
the relay in 21 minutes and hash-checked):
- Defaults: 640x480, 16-bit colour and Z, `GL_LINEAR_MIPMAP_NEAREST`.
  Started with `+set logfile 2 +set nextdemo quit +timedemo 1 +demo
  demo001`.
- DirectSound fails to start: this machine has no sound device.

| Run | Result | ICD paths |
|---|---|---|
| windowed | 1346 frames, 63.5 s: **21.2 fps** | hw 209,541 batches / 5,263,344 triangles; cpu 0; hw-refused 0; no failed draw or clear |
| fullscreen | 1346 frames, 63.0 s: **21.4 fps** | the same |

The ICD made and uploaded 355 surface textures (6,286 KB) with no
failures or evictions. One windowed frame (`boot38-q3-windowed2/q3-1.png`)
draws correctly: lightmapped walls and floor, the weapon, the HUD digits,
and the head and armour icons beside their counts.

Direct3D was checked again for the placement change, since it moved
Direct3D's Z pitch too:
- V9XD32 PASS at 32 and 16 bpp;
- V9XDDP's `*Ok` keys are unchanged from boot 36;
- `/mixed` `MixedOk=1`.

Each run was made once. The numbers are recorded, not compared.

## Boots 39 and 40: the four V9XGLP rows

V9XGLP gained readbacks:
- a row and column scan of the geometry scene's back buffer, read before
  the swap, one letter a sample (`GeomScan*`);
- the 2x2 texture's four texel centres (`Small2x2T*`).

The probe also writes the T&L frame as `V9XTNLA.BMP`.

- **`AlphaSolid`: not a fault.** The texture's right half is opaque red,
  and the read is 0xRRGGBB. Carrizo's red matches the Haswell
  (`hsw-4570-tnl-step4-2026-10-27/V9XGLP-backend-boot98.INI`). It was the
  Ironlake run that read the green clear there.
- **The T&L frame and the geometry scene: the depth clear missed each
  row's padding.**
  - Carrizo's boot 38 frame (`boot38-V9XTNLA.png`) has columns of black
    dashes, which the Haswell's does not. They sit in every sixth 8x8
    tile, in its last 24 samples. At 128 bytes a tile, that is bytes 720
    to 767 of every 768: the padding of the new 64-byte Z rows for a
    360-pixel window.
  - The whole-surface SDMA depth fill (`eng_vi.c`) filled
    `width x 2` bytes a row. In a tiled buffer, the padding belongs to
    tiles in the picture, so those samples kept stale values and failed
    the depth test.
  - Boot 37's 16-byte rows had no padding at these widths, which is why
    this appeared with the pitch fix. A whole-surface fill now covers
    the whole pitch.
  - Boot 39: the T&L frame matches (28,193 drawn pixels against the
    Haswell's 28,196, none differing past tolerance). The geometry scans
    read green left and red right on every row and column.
- **`Small2x2`: the sampler's row stride.** Row 0's texels read green and
  row 1's read `0x08E300` (boot 39).
  - A one-level texture follows the chain layout measured at boot 27: the
    sampler widens a level's rows 64 bytes at a time until the level is a
    whole number of 256-byte blocks. A 2x2 at 64-byte rows is 128 bytes,
    so the sampler reads row 1 at 128.
  - A 4x4, a 1x4 or a 4x1 is already whole blocks, or has one row, which
    is why they read right.
  - `create_surface` now places a single level by `v9x_vi_3d_mip_layout`.
    Sides that are not powers of two keep plain 64-byte rows, unmeasured.
  - Boot 40: all four texels green.

Boot 40, the final build:
- V9XGLP matches the Haswell's backend run except for multitexture (one
  unit here) and lighting. That needs a T&L engine; the Ironlake reads the
  same unlit white.
- V9XD32 PASS at 32 and 16 bpp. V9XDDP's `*Ok` keys are unchanged.
- The Quake 3 demo ran fullscreen at 21.3 fps, every batch on the
  hardware, with no failed draw or clear.

Cedar has the same shape of code, untested. `d3d_eg.c` rounds Z16 rows
to 16 bytes against a 32-byte bind alignment, so a window whose width is
an odd multiple of 8 pixels would have its depth refused. `eng_eg.c`'s
whole-surface fill also keeps the pixel width.

## Not checked

- Only one Quake 3 frame was inspected.
- Non-power-of-two textures' row stride.
- Glide uses the same explicit path, but no Glide game was run.
- There is no multitexture, since the engine has one unit, so Quake 3
  draws its lightmaps in a second pass.

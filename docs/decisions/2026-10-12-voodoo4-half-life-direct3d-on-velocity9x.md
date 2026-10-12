# Half-Life's Direct3D renderer draws correctly on the Voodoo4 engine; mipmaps are what is missing

Date: 2026-10-12. Machine: A8U4I5, boots 430 (3dfx 1.04.00 driver) and 431
(Velocity9x, `f419ed1`). Card: Voodoo4 4500 PCI. Michael ran Half-Life and
watched the monitor. Evidence:
[a8u4i5-voodoo4-d3d-2026-10-12](../probe/a8u4i5-voodoo4-d3d-2026-10-12/).
Plan: [3dfx-vsa100-d3d.md](../plans/3dfx-vsa100-d3d.md), the step 3 control
and step 5's gate game.

## The control: V9XDDP under 3dfx's own driver (boot 430)

Same tool, same desktop mode. Of 221-223 pass keys, 29 differ:

- **3dfx's passes, ours fails:**
  - mip chains A-E, mip rows, mip ladder, mip triangle, mip level
    select, trilinear blend;
  - fog (depth and texture);
  - specular (Gouraud and texture);
  - DECALALPHA (565 and 4444).

  Every one is a feature this engine refuses or does not build yet.
- **Ours passes, 3dfx's fails:** `PerspOk`.
- **Render targets:**
  - 3dfx's driver creates 800- and 1024-wide off-screen targets on the
    640 desktop; ours refuses them (`80070057`).
  - This is every Velocity9x family: the Rage XL, SiS, Matrox and Trio64
    records show the same. It is not this engine.
  - 3dfx's driver fails the 640 target's Z (`8876017C`) where ours passes.
- **The texture matrix (`TexM_*`)** has no difference.

## The gate game: Half-Life, Direct3D, 640x480x16 fullscreen, `timedemo mwd5`

| Driver | Runs (fps) | Michael |
|---|---|---|
| 3dfx 1.04.00 (boot 430) | 35.698, 58.912, 58.940 | looked right; "vsync maybe on?" |
| Velocity9x (boot 431) | 19.914, 25.719, 25.724 | "looked good but I think maybe some texture filtering not occurring, the native driver looked smoother" |

The HAL snapshot after the Velocity9x runs:

- **Refusals:** none, so every draw reached the engine.
- **Filters asked for:**
  - magnification NEAREST and LINEAR;
  - minification NEAREST, LINEAR and MIPLINEAR.
- **Load:**
  - 241 textures placed;
  - 10.4 million triangles in 885,234 batches;
  - engine valid, no timeouts.

## What this shows

1. **Half-Life's Direct3D renderer draws correctly on the engine** at the
   first attempt.
2. **What Michael saw is mipmapping.** Half-Life asks for MIPLINEAR. With
   one level, the map folds that to bilinear on level 0, so minified
   surfaces sample the full-size texture and shimmer where 3dfx's driver
   uses its smaller levels.
3. **The frame rate is recorded, not chased.** 3dfx's driver held about
   59 fps, which looks like a 60 Hz vsync cap and is not established.
   Ours ran at 25.7.

   One candidate cost is visible in the snapshot: 885,234 batches, each
   waiting for idle and resending its whole state. It is not measured.

## Not established

- Whether 3dfx's 58.9 fps is vsync-limited.
- Where the engine's time goes.
- The wide off-screen target refusal's cause, in the shared HAL.

# Rage 128: OpenGL and Glide through the render interface, on the engine where exact

Date: 2026-10-23

Status: **measured, one card, by probes only.** A8U4I5's Rage Fury Pro,
boots 530 to 534, desktop 800x600x16. This is Phase 6 of
[ati-rage128.md](../plans/ati-rage128.md). It follows the Millennium II's
[render-interface plan](../plans/matrox-mga2164w-render-interface.md),
steps 2 and 4. Evidence:
[`../probe/a8u4i5-rage128-render-interface-2026-10-23/`](../probe/a8u4i5-rage128-render-interface-2026-10-23/).

## 1. The mixed frame passes

`V9XDDP /mixed` (boot 530; the real run is in `V9XDD2`):

1. The engine draws red at depth 0.5: `F800`, `8000`.
2. The CPU stores green and `4000`: `07E0`, `4000`.
3. The engine's second draw is rejected at that pixel (`07E0`, `4000`)
   and accepted beside it (`001F`, `6000`).

`MixedOk=1`. These are the same readings as the software reference and
the MGA-2164W. The Rage 128's Z16 holds z x 65535, as the CPU's does.

`v9x_r3d_fallback` therefore returns the software engine for the Rage 128
as well. A refused render-interface draw is now drawn by the CPU, after
the drain.

Boot 531, with every explicit draw still refused:

| Probe | Result |
|---|---|
| V9XR3DP | `COMPLETE`; `DrawOk`, `TexturedScissorOk` and `MaskedOk` 1 |
| V9XGLP | `PASS`, renderer "Velocity9x Rage 128" |
| V9XGLIDP (Glide 2) | `PASS failures=0` |
| V9XGL3P (Glide 3) | `SESSION`: opens and closes |

## 2. Explicit draws on the engine

`v9x_d3d_r128_map_draw` used to refuse every explicit draw. It now maps
them, with the new cases written first and seen to fail.

The builder gained two things:

- **A scissor.** The half-open rectangle goes to `SC_TOP_LEFT_C` and
  `SC_BOTTOM_RIGHT_C`, inclusive, as Mesa's `r128UpdateClipping` writes
  them.
- **A write mask.** `PLANE_3D_MASK_C` is set per channel: RGB565 in both
  16-bit halves, ARGB8888 with the alpha byte always kept.

Every draw of this engine is undithered, Direct3D's included, so explicit
draws need nothing more for that.

Still refused and drawn by the CPU:

- CPU-resident texture levels;
- a second unit;
- a scissor that is empty or extends past the target.

## 3. The hang: the drain could not reach a running CCE

At boot 532, V9XR3DP and V9XGLP passed with explicit draws on the engine.
V9XGLIDP then hung A8U4I5, which needed a power cycle (boot 533).
`V9XGLIDE.LOG` ends at Glide's first `grLfbLock`, straight after its
first `grBufferClear`, in Glide's own 640x480 mode.

The cause, read from the code, not caught in the act:

1. `v9x_render_drain` asks the 2D engine to validate before it waits.
2. `eng_r128.c` refuses to validate while PM4 is not in direct mode, and
   it is not while Direct3D has left the CCE running
   ([previous record](2026-10-23-rage128-mip-order-flips-and-a-running-cce.md)).
3. So in a mode where the 2D engine had not yet been used, every drain
   answered ABANDONED without stopping the CCE: Lock, DestroySurface, and
   the drain before a fallback draw.
4. Glide's mode set then ran against a free-running microengine. Boot 522
   hung that way.

V9XD32 and 3DMark escaped because their first operation in a mode is a
2D clear.

The flip path had a second gap. The core's wait before a flip is the
ViRGE's, so a Rage 128 flip never drained either.

Fixes:

- validate drains the CCE before its checks;
- `v9x_r128_set_display_start` drains before it writes the new address.

Boot 534:

| Probe | Result |
|---|---|
| V9XD32, 32 and 16 bpp | `PASS` |
| V9XR3DP | `COMPLETE`, all three checks 1 |
| V9XGLP | `PASS`, `LifetimeFailures=0` |
| V9XGLIDP (Glide 2) | `PASS failures=0` |
| V9XGL3P (Glide 3) | `SESSION` |

The machine stayed up. `EngineFlags=1`, and there were no idle timeouts.

## The split

108 render-interface draws were refused by the engine and drawn by the
CPU (`M64AcceptPolicy02`, the `EXPLICIT` reason): CPU levels, the ICD's
usual texture path, or a scissor outside the target. The total is not
counted unless timing is on: `R3dListCalls` counts only the Direct3D
path, and its 34 are the two V9XD32 runs. So how many explicit draws the
engine drew is not measured. The probes cannot tell its pixels from the
CPU's either way.

## 4. Half-Life OpenGL: the describe never offered surface textures

At boot 534, Half-Life OpenGL's mwd5 timedemo ran at **0.75 fps** (394
frames in 525 s). The frames looked right in a screenshot.

`V9XGL.LOG` showed why:

- `hwtex live=0 creates=0`;
- `paths ... hw=0/0 cpu=26831/576463`.

The ICD kept every texture on the CPU. The engine refused every textured
batch (`M64AcceptPolicy02=44,174`), so the software engine drew the game.
`v9x_r3d_entry_describe` gives each engine with a surface sampler its
`hw_texture_size_max`. The Rage 128 had no branch there, so it published
0, which tells the ICD "no surface textures". The branch now offers
powers of two from 1 to 1024 in all three formats, which is what
`v9x_d3d_r128_create_surface` places.

Boot 535, after the fix:

- The four probes pass as before.
- Half-Life OpenGL mwd5 runs at **12.0 fps** cold and **14.1** warm
  (twice). ATI's driver: 61.1 cold, 70.1 to 70.3 warm.
- `paths hw=190383/6633295 cpu=0/0 hw-refused=0/0`; `hwtex live=214`,
  `uploads=696` (`partial=456`), none failed.
- Two mid-demo screenshots look right: Xen terrain, filtered walls,
  lightmaps, sky, and the HUD (`hlgl-mwd5-b535.png`).
- Michael then ran it himself and watched it on the monitor: "hl1
  working now in opengl". His runs: 13.44 fps, then 14.12.
- The performance is parked for this release (Michael, 2026-10-23).

## Not done

- A Glide game has not been run.
- The frame rate is recorded, not investigated (parked). The batches still go
  through CPU PIO, and every partial texture upload (a lightmap) drains
  the CCE through Lock.
- 32 bpp under OpenGL, as on the MGA.
- The bus-master ring, which Michael will review later.

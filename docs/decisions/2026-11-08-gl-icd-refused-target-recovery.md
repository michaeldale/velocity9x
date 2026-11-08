# The ICD no longer drops a refused target silently

Date: 2026-11-08

Status: **measured on two machines.**

- M-D7TFW4, Trinity, desktop at 16 bpp:
  - boot 64 ran the 0.41.0 `ati` files with only `V9XGL.DLL` replaced, to
    test the recovery against a HAL that still refuses;
  - boot 65 runs the whole new build.
- A8U4I5, Rage 128 Pro, desktop at 32 bpp: boot 681 runs the whole new build.

All builds are `26a90ca5-dirty`, and each install's SHA-256 matched the
package, 8 of 8. Reports:
[`../probe/m-d7tfw4-gl-target-recover-2026-11-08/`](../probe/m-d7tfw4-gl-target-recover-2026-11-08/).
This follows
[the placement record](2026-11-08-render-target-placement-trinity-rage128.md).

## What was wrong

On Trinity, the HAL refused the 360-pixel back buffer as a target. Every
clear and draw into it then came back `V9X_R3D_RESULT_INVALID`, and the
ICD did nothing with that:

- **Clears:** one `glClear result=1` line was logged per clear, and the
  clear was skipped.
- **Draws:** they were counted under `failed ... other=79`, and the batch
  was skipped.

The window kept whatever it showed before, and the application got no GL
error. The draw path retries only `UNSUPPORTED`: first with the CPU
texture copy, then unfogged, then as split passes. The interface's
contract says INVALID means a malformed request, but the HAL also returns
it from `v9x_r3d_bind` for a target its engine will not take. Evergreen
has no software fallback in the HAL (`v9x_r3d_fallback` covers the GMA,
the Matrox engines and the Rage 128), so nothing could draw there.

## What the ICD does now (`gl_icd.c`, `gl_surface.c`)

1. **Tells the two INVALIDs apart.** After a clear or draw comes back
   INVALID, `v9x_gl_target_recover` sends a clear with no colour and no
   depth. The HAL binds the target for it and then writes nothing
   (`r3d_clear.c`). Only if that clear is refused too is the target the
   cause. A malformed batch never reaches step 2, so it never costs the
   back buffer's contents.
2. **Pads once.** `v9x_gl_drawable_pad` makes the back and depth buffers
   again at the window's width rounded up to 64 pixels. That is 128 bytes
   a row at 16 bpp and 256 at 32, more than any engine's measured row
   alignment. The front copy is made at the same width. Once per window
   size: a padded drawable is not padded again, and a resize starts from
   the window's width.
3. **Retries the same clear or draw** on the new surfaces. INVALID is
   decided before anything reaches the engine, so nothing is drawn twice.
4. **Keeps the window's size everywhere GL sees it:** viewport, reads and
   the presented rectangle. Present and the front-buffer copies now blit
   a window-sized source rectangle (`v9x_gl_drawable_image_rect`). Before,
   they blitted the whole surface, which would have scaled the padding
   into the window.
5. **Counts and logs.** A new counters line reads `counters refused
   draw-invalid= clear-invalid= clear-other= targets-refused=
   targets-padded=`. Failed clears are counted by result, and only the
   first six are logged, as failed draws already were.

## Measured

| Boot | HAL | V9XGLP at 360 wide | `targets-refused` / `padded` |
|---|---|---|---|
| Trinity 62, before | 0.41.0 | every read after the resize white | (no counter) |
| Trinity 64 | 0.41.0 | `PASS`, `LifetimeClearAfter=0000FF`, Cva PASS | 1 / 1 |
| Trinity 65 | new | `PASS`, the same values | 0 / 0 |
| A8U4I5 681 | new | `PASS`, Cva PASS, TNL match | 0 / 0 |

On boot 64 the ICD logged `drawable 360x280 refused as a target,
surfaces now 384 wide` once. Every key of that V9XGLP report then
matched boot 63's, where the HAL itself placed the target correctly,
apart from build and timing lines.

The full build also passes the other probes:

- Trinity, boot 65: V9XD32 `PASS`.
- A8U4I5, boot 681: V9XD32 `PASS`, V9XGLIDP `PASS failures=0` and
  V9XR3DP `COMPLETE`.

## Not measured

- **The padded image on screen.** V9XGLP reads the back buffer. Nobody
  looked at a presented frame from padded surfaces, so the
  source-rectangle blit is untested by eye.
- **A depth buffer refused alone.** The check clear binds the target
  only, so this is not recovered. It is counted as `draw-invalid`.
- **No GL error is raised.** A clear or draw that still fails after the
  pad is only counted and logged.
- **A game.**

# An unsynced Gen3 flip held until its blank: VSync=2 no longer flickers

Date: 2026-11-07. Machine: the netbook (W9QI2BE2, 945GSE / GMA 950),
boot 285, build `efd2fa90`, SYSTEM.INI `[Velocity9x] VSync=2` (Always
off). Issue: [flicker in 3DMark 2001 SE](../issues/2026-11-01-gma950-3dmark2001se-flicker-refused-vertices.md).
Evidence: [`../probe/gen3-unsynced-flip-2026-11-07/`](../probe/gen3-unsynced-flip-2026-11-07/).

## The change

Gen3's plane base is double-buffered and is taken at the start of the
vertical blank, however it is written. Before this change, an unsynced
flip (`DDFLIP_NOVSYNC`, or `VSync=2`) left nothing pending, so the
buffer it took off the screen went back to the game at once, while the
panel was still scanning it.

- **Policy.** `v9x_vsync_flip_stays_pending` (`vsync.c`, pure,
  `test_which_flips_stay_pending`) holds a flip pending when it is
  synced, and when it is unsynced on a scanout whose base latches at the
  blank. The test was written first and failed on that case.
- **Hook.** `v9x_scanout_unsynced_base_latches` claims the latching for
  the i9xx plane only (Gen3 to Gen5). Haswell and the others were not
  measured and keep the old behaviour.
- **Core.** `v9x_flip_body` arms the flip from the policy. Blt, Lock,
  draws and the next Flip then wait for it, as they do for a synced
  flip. The base is still written at once, without waiting for the flip
  window.
- **Completion.** A flip is complete one frame tick after its write. An
  unsynced write that lands after the latch line is taken a blank later,
  so it now needs two ticks.
- **Counter.** `FlipUnsyncedHeld` counts the flips the core held.

Gates: `check-tree.ps1`, `build-host.ps1` and `run-checks.ps1` passed.
Deployed by WININIT rename and `reset`. All eight installed files match
the package by SHA-256; the boot reached `enable-ok`. V9XD32 PASS at DDI
6 and 5.

## Results, 3DMark 2001 SE default, 640x480x16, every test

| | `VSync=2`, `089a5e70` (boot 284) | `VSync=0`, `089a5e70` (boot 284) | `VSync=2`, `efd2fa90` (boot 285) |
|---|---|---|---|
| flicker on the panel (Michael) | yes | **no** | **no** |
| flips | about 4,615 | 2,983 | 2,963 |
| unsynced / held | all / (not held) | 0 / - | all / **all** |
| clears that waited for a flip | 0 | 2,477 | 2,480 |
| draws that waited for a flip | 0 | 12 | 12 |
| batches refused | 0 | 0 | 0 |
| `DrawsIntoPresented` | +11 | +11 | 0 |
| score (recorded, not compared) | 683 | (not read) | 627 |

Readings:
- **Always off now behaves like Game decides** for the buffers. Clears
  wait for the flip about as often (2,480 against 2,477), and the panel
  shows no frame being cleared.
- **What Always off still does:** the base is written at once, wherever
  the beam is. Because the base latches at the blank, that gains nothing
  visible on Gen3. A second flip in one frame now waits for the first.
- **Not established:** why `DrawsIntoPresented` read 0 on this run and 11
  on both runs of boot 284.

## Also seen

The first attempt on boot 285 stopped before the first test with
3DMark's "FATAL ERROR: failed to validate material"
(`3dmark-error.log`). By then the boot had run 6 flips, all held, and
55 batches, none refused (`after-error.INI` against `pre.INI`). Dismissed
and started again, the benchmark ran to the end. Seen once, not
reproduced. Whether the held flip had any part in it is not known: no
earlier `VSync=2` run showed it, and nothing traced it.

## Not done

- The GMA 900 and GMA 3150, and Gen4 and Gen5, which take the same hook.
- Haswell and the other scanouts, whose unsynced flips may release a
  buffer early in the same way; not looked at.
- Tearing itself was not looked for. The panel showed no flicker.

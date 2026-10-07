# Rage XL hard lock on UT99's first OpenGL frame after a fresh boot

Filed: 2026-10-07
Status: unreproduced
Found in: A8U4I5, ATI Rage XL PCI (1002:4752), Velocity9x 0.12.1 (927927b-dirty ICD), boot 330

## What happened

UT99 436 was started windowed (800x585 client on the 800x600 desktop) with
`OpenGLDrv.OpenGLRenderDevice`, about a minute after a warm restart. The
machine hard locked and had to be power cycled by hand.

`C:\V9XDIAG\V9XGL.LOG` from that run stops after line 66: pixel format set,
context created, drawable `800x585`, `DrvSetContext -> 1`, then the first
`state tex=1` line. The first `time interval` summary never arrived, so the
lock was in the first draw, its texture upload, or the first swap.

## What argues against the day's ICD changes

- The primary-recovery path runs only when the primary is lost; nothing had
  been lost yet.
- The screen-size clamp does not change an 800x585 drawable on an 800x600
  screen.
- The same windowed start on the 0.12.0 ICD worked earlier the same day,
  and the 0.12.1 ICD started cleanly on the very next boot (331) and then
  survived the fullscreen switch.

## Still unknown

- Whether it is specific to the first 3D use after boot, or intermittent at
  any time.
- Whether the 0.12.0 ICD hangs the same way under the same conditions.
- The run that worked on boot 331 spent its first ~3 minutes with no frame,
  thrashing texture memory (974 `hwtex create` failures,
  `DDERR_OUTOFVIDEOMEMORY`, 800x585 back and depth buffers on an 8 MB
  card). A lock inside that eviction churn is a candidate, unproved.

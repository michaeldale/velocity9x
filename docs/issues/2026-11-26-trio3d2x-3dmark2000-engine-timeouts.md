# Trio3D/2X: 3DMark 2000 at 640x480 completes with 51 engine idle timeouts and resets

Date: 2026-11-26. Machine: A8U4I5, S3 Trio3D/2X (`5333:8A13`, 4 MiB),
the 0.61.0 tree's `s3` set, boot 700. Status: open, cause not looked
at. First 3DMark 2000 run on this card.

## What is seen

3DMark 2000's default benchmark, set to 640x480x16 with 16-bit textures
and Z and a triple buffer (the default 1024x768x32 needs 16 MB), runs to
its score (175). Across the run, from V9XTRACE snapshots before and
after: `EngineIdleTimeouts` 0 to 51, `EngineResets` 0 to 51,
`TrianglesDeclined` 9, `FlipHandled` 1,676
([snapshots](../probe/1.0-round-trio3d2x-hd5770-2026-11-26/trio3d2x-3dmark-640/)).
3DMark 99 on this card in September had none
([issue](2026-09-03-3dmark99-on-the-trio3d-after-the-stride-fix.md)).

Where in the run they fell and what was on screen then is not known: no
frames were grabbed and nobody watched the monitor.

## To find out

Run again with V9XTRACE grabbing (the runner's `-ThreeDMark` path, once
it can start 3DMark at 640x480) or with OBS on the monitor, and read
which test the counters move in. Record:
[decision](../decisions/2026-11-26-1.0-trio3d2x-hd5770.md).

## 2026-11-27: the ViRGE/DX times out at 640x480 too, not at 800x600

The ViRGE/DX, on the same S3D path, ran 3DMark 2000 with 45 engine idle
timeouts and resets at 640x480x16 and none at 800x600x16 on the same
boot ([issue](2026-11-27-virgedx-3dmark2000-640x480-engine-timeouts.md)).
The Trio3D/2X's 51 may be the same 640x480 effect; it has not been run
at 800x600.

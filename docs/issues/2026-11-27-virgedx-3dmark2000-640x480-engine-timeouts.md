# ViRGE/DX: 3DMark 2000 at 640x480 runs with 45 engine idle timeouts, none at 800x600

Date: 2026-11-27. Machine: A8U4I5, S3 ViRGE/DX (`5333:8A01`, 4 MiB),
the 0.61.0 tree's `s3` set. Status: open, cause not looked at.

## What is seen

3DMark 2000, default tests, 16-bit Z, triple buffered, both runs on
boot 730 by hand:

| Setting | Engine idle timeouts | Engine resets | Flips |
|---|---|---|---|
| 640x480x16 | 45 | 45 | 1,505 |
| 800x600x16 | 0 | 0 | 1,350 |

Both ran to a score. November's run on this card at 800x600 had no
timeout ([record](../decisions/2026-11-04-virge-dx-silicon-3d-and-mip-filter-swap.md)),
so the difference is the resolution, not a change since. The
snapshots (`docs/probe/virgedx-1.0-2026-11-27/virgedx-3dmark-640/`,
`virgedx-3dmark-800/`) count the timeouts but do not say which test or
draw they followed.

The Trio3D/2X, on the same S3D path, had 51 timeouts and resets in its
only 3DMark 2000 run, also at 640x480
([issue](2026-11-26-trio3d2x-3dmark2000-engine-timeouts.md)); whether
it is clean at 800x600 was not run (it has 4 MiB too).

## To find out

Which test the timeouts fall in (a V9XTRACE ring entry per timeout, or
3DMark's tests run one at a time), and what differs at 640x480: the
3D engine's destination pitch or the triple buffers' placement in
4 MiB. Record: [decision](../decisions/2026-11-27-1.0-virgedx-a8u4i5.md).

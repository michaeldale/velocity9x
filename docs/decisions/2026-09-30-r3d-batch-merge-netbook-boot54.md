# In-call batch merging on the netbook: no regression, and 3DMark 99 never exercises it

Date: 2026-09-30. Machine: MICHAEL-NETBOOK, 945GSE / GMA 950 `8086:27AE`
revision 03, Windows 98 SE, boot 54. Build `010b2d6-dirty`, the tree
committed as `08b4239`: in-call merging (`docs/plans/r3d-in-call-batch-merging.md`
phases 1-2) with the diagnostics ABI `2026093001` list counters. Synchronous
submission (`AsyncEnabled=0`). Evidence:
`../probe/intel-gma950-r3d-batch-merge-2026-09-30/`.

## What was run

- The broad DirectDraw/Direct3D probe: `Result=COMPLETE`, 52 `*_Ok=1` keys
  in `V9XDD.INI` and 73 in `V9XDD2.INI`, no `*_Ok=0`. The post-probe
  snapshot has no engine, breadcrumb, ring-space or refusal counter set.
- 3DMark 99 Max, default project, at 1024x576x16 and then 640x480x16. Both
  completed; the score dialogs are kept (759 / 16459 CPU and 1010 / 16381
  CPU). Scores are recorded, not compared. No frame capture or photograph of
  the run itself was kept, so visual correctness rests on completion only.

## What the counters say

| Snapshot after | `R3dListCalls` | `TrianglesIn` | `Culled` | `Clipped` | `SinkBatches` |
|---|---|---|---|---|---|
| probe | 566 | 569 | 0 | 0 | 566 |
| 3DMark 1024x576 | 362,427 | 15,256,953 | 0 | 0 | 362,427 |
| 3DMark 640x480 | 700,120 | 27,516,976 | 0 | 0 | 700,120 |

Counters are cumulative for the boot. Nothing was culled or clipped in the
list builder, and every list call produced exactly one sink batch of about
40 triangles. The merge path starts only at a list's first break, so it
never ran: these runs exercise the unbroken, no-copy window and the new
counters, not merging. Gen3 has `clip_in_core = 0`, so no Gen3 run can test
the clipped-fan ordering either.

`I9xxDrawsRefused` rose to 390 and then 600, every one reason 6
(`V9X_I9XX_REFUSE_VERTICES`, the runtime run builder). This is not a new
failure: the 2026-09-25 mip-tree snapshots of the same benchmark have 216
(1024x576) and 728 (640x480), also reason 6. The boot 50 control reads 0
because its snapshots were taken after the probe, not after 3DMark. Which
3DMark draws the run builder rejects is unmeasured.

## Standing

No regression was seen on Gen3: the probe, both 3DMark runs and Quake 2
completed with no failure counter set. That covers lists that never break.
Whether merging is correct when it runs, and whether it helps, is still
open. The plan's Phase 0 question (does culling split Half-Life's lists?)
needs Half-Life, and the fan ordering needs a clip-in-core engine (Mach64 or
ViRGE).

## An unidentified Direct3D run between 3DMark and Quake 2

The snapshot taken before Quake 2 (`boot54-pre-quake2-V9XSNA7.INI`) has
moved on from the 640x480 3DMark one by 2,386,571 list calls, 24,902,235
triangles, 6,121,670 culled and 2,359,518 sink batches. Some Direct3D
application ran in between; which one is not recorded. Its lists did break
(a quarter of its triangles culled), yet it produced 0.99 sink batches a
call. Without merging, each culled run inside a list would have ended a
batch. This is the only evidence so far that merging runs on hardware, and
it has no control run, no frame capture and no named workload.

## Quake 2 on the same boot

The Quake 2 demo in `C:\Q2Demo`, with its existing configuration: `vid_ref gl`,
`gl_mode 3`, fullscreen 640x480, 16-bit colour and Z. Launched with
`+set vid_ref gl +set logfile 2 +set timedemo 1` and no map, so the attract
loop replays `demo2` as fast as it can:

| Pass | Frames | Seconds | fps |
|---|---|---|---|
| 1 | 632 | 36.8 | 17.2 |
| 2 | 632 | 36.6 | 17.3 |
| 3 | 632 | 36.6 | 17.2 |

A fourth pass was cut short by `quit`. Across the run the ring took 191,009
breadcrumb submits with no breadcrumb, engine or ring-space timeout and no
new refusal. `config.cfg` was restored afterwards.

The ICD reaches the list builder through the render interface
(`v9x_r3d_draw_body`), which has zero staging capacity, so this run tests
the builder's unchanged path, not merging. The five list counters are
updated only by the Direct3D wrapper (`v9x_d3d_draw_list`), which explains
why they did not move; they do not cover the ICD. There is no earlier
measurement of this timedemo in the repository. The operator remembered
about 17 fps from an earlier run of the same timedemo, but not which build
ran it, so a control run followed.

### Control run

The control is a clean build of `010b2d6`: the tree just before the merge
change, whose code matches `dd9d22b`. DRV, VXD, HAL and settings page were
built from it and swapped in together with WININIT renames, along with a
V9XTRACE from the same build, because the change bumped the shared ABI. The
installed ICD was left as it was for every run. Boot 55 ran the control on a
fresh boot. Boot 56 restored the boot 54 binaries, hash-checked against the
files taken off the machine beforehand, and repeated the run on a fresh
boot, so both builds have a run with no earlier 3DMark in the same boot.

| Boot | Build | Seconds per pass | fps |
|---|---|---|---|
| 54 (after 3DMark) | merge | 36.8, 36.6, 36.6 | 17.2, 17.3, 17.2 |
| 55 (fresh) | control | 36.9, 36.8, 36.7 | 17.1, 17.2, 17.2 |
| 56 (fresh) | merge | 36.7, 36.5, 36.4 | 17.2, 17.3, 17.3 |

On the two fresh boots the merge build is 0.2-0.3 s faster per pass, which
is about 1%. The spread within each boot's three passes is 0.2-0.3 s, so
this is not claimed as a gain. The finding is that the change costs Quake 2
nothing measurable. That is expected: the ICD's list calls take the
zero-capacity path, which the change was meant to keep identical.
`R3dListCalls` stayed at 0 through the boot 56 run, which confirms that
Quake 2 makes no Direct3D list calls. No run set a timeout, abandonment or
refusal counter. `config.cfg` was restored after each run. The 28.4 fps recorded on 2026-09-26 is
`timerefresh` at the `demo1` spawn point, which is a different measurement.

Evidence, in the probe folder: `boot54-quake2-timedemo-qconsole.log`,
`boot55-control-quake2-timedemo-qconsole.log`,
`boot56-quake2-timedemo-qconsole.log`, and each boot's pre/post Quake 2
snapshots.

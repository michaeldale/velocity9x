# The HD 4000 mobile hard-locks a minute into 3DMark 2000's Game 1

Date: 2026-11-05. Status: **open; bisected 2026-11-06 to the two-stage path
(`45bfb395`)**. The kernel or state that stops the GPU is not found yet.

Machine: W9QI75E0 (`M-ZJQ2DX`), HD 4000 mobile `8086:0166`, LVDS panel on
pipe A. Driver `aa70a3a9` from branch `intel-ivb-mobile`, boot 5. Plan:
`docs\plans\intel-ivybridge-mobile-0166.md`.

Captures: `docs\probe\w9qi75e0-ivb-mobile-3dmark2000-2026-11-05\`.

## What happened

- 3DMark 2000 v1.1 (free edition, installed this session), Run Default
  Benchmark at about 22:42 local, from the 1366x768x16 desktop.
- The agent answered a `wait-window` poll every second until 22:43:04
  (`AGENT.LOG`, boot 5), then nothing. Its last relay check-in was
  11:42:28Z.
- The owner found it "stuck mid test on helicopter test", the first Game 1
  scene, and power-cycled it. Boot 6 started at 23:08:16.
- Nothing on disk records the GPU's state: the HAL keeps no running trace,
  and `V9XDDT.TXT` is `V9XDDP`'s from before the run.

Everything before it on boot 5 passed: 27 of 27 flips taken, `V9XD32` at
16 and 32 bpp with Z16 and Z24, the GL, render-interface and Glide probes
(`2026-11-05-w9qi75e0-ivb-mobile-intel-gma.md`, `-gl-glide.md`).

## Hypotheses

1. **A Gen7 regression from Haswell-only work since 21 October.** The
   desktop HD 4000 (W9QI4E14) ran 3DMark 2000 to its score (1910) on
   `178201b9-dirty`. Since then 24 commits have changed the Gen7 3D path,
   all measured on the HD 4600 only, and several change exactly what Game 1
   sends: indexed draws drawn whole and culled by the chip (`8574abcc`),
   drawing inside the guard band (`1d8d3045`), two texture stages
   (`45bfb395`), Direct3D draws outside the GDI primary (`6ce432cb`), and
   the per-batch flush change (`3fd984d6`). None of those but `3fd984d6`
   has an Ivy Bridge branch. W9QI4E14 has not run any of them; it has
   been parked and offline since 2026-10-28.
2. **The panel and the full-screen mode.** 3DMark sets 1024x768x16 through
   VBE; on this machine the BIOS scales it to the 1366x768 panel with
   fitter 0. W9QI4E14 drove a CRT. Against it: `V9XD32` set its own
   full-screen modes and flipped on this machine without trouble, and the
   HAL reads only pipe A, gated on pipe A being live. The GMA 950 lock of
   GitHub issue 6 was a read of a disabled pipe; nothing on this path reads
   pipe B or C.
3. **Something in Game 1's workload neither probe covers** (fog, larger
   textures, longer batches) that also hangs the desktop part on today's
   code. Hypothesis 1 would include this.

## Next

Each hang costs the owner a power cycle, so the order matters:

1. Ask before the next hang-risking run.
2. Bisect with HAL-only builds swapped in by `WININIT.INI` (the DRV, VxD and
   the shared ABI stay `aa70a3a9`): first `aa70a3a9` with the Gen7 indexed,
   guard-band and two-stage paths routed back to their pre-`8574abcc`
   behaviour on Ivy Bridge. If Game 1 then runs, split those three.
3. Run 3DMark 2000 windowed or at 1366x768, if hypothesis 1 survives,
   to separate the full-screen mode change.

## Bisect (2026-11-06, boots 7-12)

Whole-HAL bisection by commit does not work: the HALs built at
`178201b9` and at `9a3a8178` (the desktop's 3DMark build, committed)
load beside the `aa70a3a9` driver but offer no Direct3D device, and
3DMark 2000 refuses to start ("requires a DirectX 7 compatible
3D-accelerator"; boots 7 and 8). Why was not looked into.

So each run below is `aa70a3a9`'s HAL with a measurement patch in
`d3d_hsw.c` that changes Ivy Bridge only (never committed). Each went in
as `V9XHAL.DLL` alone by a plain `WININIT.INI` rename, checked by SHA-256
after the `reset`; `V9XD32` passed on each before 3DMark ran. The owner
was told before the first run.

| Boot | Variant | Guard band | Two stages | Indexed | 3DMark 2000 default run |
|---|---|---|---|---|---|
| 5 | as shipped | on | on | on | hard lock ~1 min in (owner power-cycled) |
| 9 | 1 | off (core clips) | off | declined | completed, 2492 |
| 10 | 2 | **on** | off | declined | completed, 2532 |
| 11 | 3 | on | **on** | declined | **stalled**: no agent log line for 19 min from ~30 s in, then the desktop with 3DMark gone, same boot |
| 12 | 4 | on | off | **on** | completed, 2719 |

Scores are recorded, not compared. Each completed run took 5.5 min
(`wait-window` on "Overall Score").

So the guard band (`1d8d3045`) and whole indexed draws (`8574abcc`) are
cleared on this machine; the two Direct3D texture stages in one pass
(`45bfb395`) are what Game 1 cannot survive on the HD 4000. W9QI4E14's
3DMark pass predates that commit.

Boot 11 recovered without a reboot, unlike boot 5. The owner has been
asked whether he ended 3DMark himself.

Not yet known: which stage pairing Game 1 asks for. `V9XD32` T12 (two
stages) and the render interface's two-unit test pass on this machine,
so it is a pairing they do not draw, or its kernel, sampler or PS state
on Gen7. `TwoUnitBatches` read 266 in every snapshot, the same with one
stage, so it counts `V9XD32`'s render-interface run, not 3DMark.

Captures: `boot9-variant1-*`, `boot10-variant2-*`, `boot11-variant3-*`,
`boot12-variant4-*` and `AGENT-boot11.LOG`.

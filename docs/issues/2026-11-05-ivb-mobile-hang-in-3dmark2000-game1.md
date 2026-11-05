# The HD 4000 mobile hard-locks a minute into 3DMark 2000's Game 1

Date: 2026-11-05. Status: **open**, seen once, cause not found.

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

# Where a Gen3 submission's time goes after async and write-combining: the CPU, not the GPU

Date: 2026-11-01

Machine: the netbook (W9QI2BE2, 945GSE / GMA 950, Atom N280 1.66 GHz),
boot 260, the 0.34.0 release-check build `94063db2` (the code on main),
`BuildsAgree=1`. V9XTRACE from `C:\V9XREL`, the release check's.

Plan: [intel-gen3-batch-cost.md](../plans/intel-gen3-batch-cost.md), step 0.
Evidence: [`../probe/gen3-batch-cost-step0-2026-11-01/`](../probe/gen3-batch-cost-step0-2026-11-01/).

## Why

The plan rests on the [2026-10-01 profile](2026-10-01-hal-submission-profile-on-the-netbook.md):
76.4 % of Half-Life D3D submissions repeated the previous state, and 52.8 of
84.3 µs a submission was waiting for the GPU. That ran before asynchronous
submission and write-combining. Step 0 measures again before any code.

## How it was run

- Half-Life mwd5, full screen, 640x480. Michael launched it and typed the
  timedemos; `hlwatch.ps1` took a V9XTRACE snapshot when the log said
  "Playing demo" and another when the fps line landed. `hlprof2.py` is the 10-01 `hlprof.py` reading decimal fields with leading
  zeros and splitting the asynchronous waits.
- The pre snapshot landed a few seconds into each demo (the log poll and
  the V9XTRACE exec take that long), so each window holds most of one demo
  and a few seconds of the console after it. Per-submission figures do not
  depend on that; shares of the window do.
- dispbench `082ccfd` (rebuilt, clean), all tools, at 16 and 32 bpp.

## Half-Life

| per submission | 10-01 D3D (sync, no WC) | D3D cold | OpenGL cold |
|---|---:|---:|---:|
| timedemo fps | 29.06 | 37.8 | 21.0 |
| window, submissions | - | 8.5 s, 65,604 | 17.6 s, 51,760 |
| HAL entry, share of the window | 51.5 % | 37.8 % | 11.7 % |
| whole HAL entry | 84.3 µs | 48.8 µs | 39.9 µs |
| front end (entry less engine draw) | 10.0 | **20.1** | 12.5 |
| build (rest of the engine draw) | 7.3 | 9.9 | 11.1 |
| decode (the allowlist) | 5.1 | 5.7 | 7.3 |
| ring write | 12.6 | 2.9 | 3.3 |
| waiting on the GPU (head/ring space + drain) | 52.8 | 10.2 | 5.8 |
| state / primitive dwords | 61.3 / 241.0 | 65.8 / 283.7 | 74.6 / 330.8 |
| state identical to the previous submission | 76.4 % | **56.7 %** | **8.2 %** |
| dwords that differ, per compared block | 0.5 | 1.0 | 2.2 |
| refusals, breadcrumb timeouts, resets | 0 | 0 | 0 |

A warm D3D timedemo in the same session ran at **47.7 fps**. Its window is
75 s with the demo 8.2 s of it, the rest Half-Life idle at the console,
still submitting about 3,900 draws a second with 97 % of them repeating
their state; it is kept (`nbd3d-warm-*`, `nbd3d-idle-after-demo-*`) but
gives no split of the demo. Subtracting the idle rate yields a submission
rate the CPU cannot reach, so the idle rate is not steady and no correction
is applied.

## DB3D

The HAL's per-row split was not recorded: DB3D refused the snapshot
(`HalCounters=skipped: snapshot size 6800 abi 2026102002, DB3D reads size
6784 abi 2026102002`). The shared trace snapshot grew by 16 bytes after
dispbench's offsets were derived, without the ABI number changing. Where
the 16 bytes came from was not traced. DB3D's own figures stand
(640x480x16, cycles at 1.66 GHz):

| row | cyc/tri | |
|---|---:|---|
| StateChange.Texture Static.1 / Change.1 | 4,993 / 26,182 | |
| StateChange.Blend Static.1 / Change.1 | 4,356 / 23,071 | |
| StateChange.Depth Static.1 / Change.1 | 4,437 / 23,830 | |
| BatchCost.Tiny | 4,352 cyc/call | |
| TriBatch.1 / .64 / .4096 | 4,399 / 2,015 / 1,926 | 10-25 WC: 4,333 / 2,016 / 1,914 |

A one-triangle batch that changes state costs about 19,000 cycles (11 to
13 µs) more than one that does not; a texture change about 3,000 cycles
more than a blend or depth change. The TriBatch rows match the 10-25
write-combining run within a percent.

## What it says

- **A D3D submission is now limited by the CPU.** Of 48.8 µs, about 39 µs
  is CPU work: the shared core's front end 20.1, the build 9.9, the decode
  5.7, the ring write 2.9. Waiting on the GPU is 10.2 µs. Async submission
  and write-combining moved the time from the GPU wait into nothing; what
  is left is the CPU's.
- **The largest single cost is not in the engine.** The front end, the
  shared core's work before `v9x_d3d_i9xx_draw`, doubled its share from
  10.0 to 20.1 µs a submission. Why it grew was not measured; the core has
  changed a great deal since 10-01 (guard band and indexed paths, record
  merging, the DX6 filter derivation).
- **The repeat rate fell**: 56.7 % in D3D, 8.2 % in OpenGL, against 76.4
  and 80.0 %. Not explained. The 0.34.0 filter change (DX6 filter words) is
  one candidate for D3D; Half-Life OpenGL now alternates textures through
  the ARB/SGIS multitexture path, which the 10-01 ICD did not have.
- **The plan's step 2 as written has little to save.** It keeps building
  and decoding the full stream and only cuts the repeated state from the
  ring: about 66 dwords of a 2.9 µs ring write in 57 % of D3D submissions,
  plus whatever the GPU spends parsing them, inside a 10 µs GPU wait. For
  OpenGL, 8 % of submissions would qualify.

## Hypotheses this disputes

- "Three submissions in four repeat their state" (10-01): no longer true
  for D3D, and far from true for OpenGL.
- "The wait is the largest single cost" (10-01): true then, not now.

## Not established

- One run per renderer, cold; windows partial as described.
- 3DMark 2000 was not run.
- How much of the 10.2 µs GPU wait the state parse and the texture flush
  are: that is the plan's step 1 variant measurement, not done.
- Where the front end's 20 µs goes.

## Consequence for the plan

The plan's own rule applies: the render drain is small and the CPU
dominates, so steps 1 and 2 are re-ranked before any code. Whether to go
on with them, or move to the CPU costs (the core's front end, and skipping
the build and decode of an unchanged state block), is Michael's decision.

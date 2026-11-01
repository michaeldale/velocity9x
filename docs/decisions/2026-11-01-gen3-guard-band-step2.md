# Gen3 under a 1,024-pixel guard band: Half-Life and 3DMark 2000 clean on fresh boots; one MPREXE fault not reproduced

Date: 2026-11-01

Machine: the netbook (W9QI2BE2, 945GSE / GMA 950, Atom N280),
build `3c736490-dirty` (the step 1 build), boots 263-266.
Plan: [intel-gen3-guard-band.md](../plans/intel-gen3-guard-band.md),
step 2. Step 1: [record](2026-11-01-gen3-guard-band-step1.md).
Evidence: [`../probe/gen3-guard-band-2026-11-01/`](../probe/gen3-guard-band-2026-11-01/)
(`step2/`, `step2-ab/`, scripts `gbstep2a.ps1`, `abrun.ps1`,
`mark2000.ps1`, `hlprof.ps1`).

The GUARD variant at a 1,024-pixel band with no inset: mask `0x04000100`
set by `V9XTRACE -omit:67109120`. Under it the core clips nothing for
Gen3, and a batch with any vertex beyond 1,024 pixels of the target is
refused whole.

## The first run, boot 263: a fault, then a stop

V9XD32 `/ddi6` and `/ddi5` PASS under the band, then Half-Life D3D clean
(49.2 fps; no clipping, no refusals). Then, starting 3DMark 2000, MPREXE
(the Windows network provider router) raised a general protection fault
in USER.EXE at `0004:00001e66` (`step2/3dmark2000/3dmark-score.png`).
3DMark did no Direct3D work at all in that window (every 3D counter zero
between the snapshots). Because the Rage 128's withdrawn band failed by
corruption showing up elsewhere, the run was stopped there, the mask set
to 0, and Michael asked for an A/B on fresh boots.

## The A/B

Each arm: `reset`, the mask set, Half-Life mwd5 D3D (warm demo, then a
profiled one), 3DMark 2000 (1024x576x16, all tests but image quality),
the visible windows listed after each.

| arm | boot | mask | Half-Life profiled demo | front end / HAL entry, µs a submission | 3DMark 2000 | clipped in the core (3DMark list calls) | refusals, timeouts, resets, front draws | fault dialogs |
|---|---:|---|---:|---|---:|---:|---|---|
| a1 | 264 | 0 | - | - | - | - | - | none; run void (below) |
| a2 | 264 | 0 | 47.71 fps | 19.6 / 45.5 | 1187 | 558,202 | 0 | none |
| b1 | 265 | band | 49.73 fps | 14.9 / 41.6 | 1214 | 0 | 0 | none |
| b2 | 266 | band | 49.78 fps | 14.8 / 41.7 | 1217 | 0 | 0 | none |

a1 is void: the WG111v2 wifi utility put up its window after the reset
and took the focus, and Half-Life's full-screen demo minimised and
stalled. a2 is the same boot, Half-Life closed and run again. Later arms
wait for the utility's window first.

3DMark scores are recorded, not compared. Its frames were not looked at
(screenshots are blind while it flips); the counters are the evidence.

## Readings

- **The MPREXE fault did not reproduce** in two guard arms on fresh boots,
  each running the same Half-Life and 3DMark 2000 sequence that preceded
  it. It is not shown to be the band, and not shown not to be: one event
  in three guard runs.
- **Under the band nothing is clipped in the core and nothing is
  refused**: every vertex Half-Life and 3DMark 2000 send is within 1,024
  pixels of the target.
- **Half-Life's front end falls from 19.6 to 14.8-14.9 µs a submission**,
  the HAL entry from 45.5 to 41.6-41.7, and the profiled demo from 47.7 to
  49.7-49.8 fps, on these fresh boots.

## Not established

- UT99's intro (the DX5 off-target case of `0bdf0c1a`) and 3DMark 2001
  SE (the Rage 128's failure) were not run; they are the rest of step 2.
- Frames under the band were not inspected, in 3DMark or Half-Life.
- The cause of the MPREXE fault.

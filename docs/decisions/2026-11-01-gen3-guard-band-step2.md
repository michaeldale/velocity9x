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

## UT99 and 3DMark 2001 SE (boot 266, added the same day)

Scripts `utrun.ps1`, `mark2001.ps1`; evidence `step2-ut99/`,
`step2-3dmark2001/`, `step2-3dmark2001-mask0/`.

| | mask 0 | band 1,024 |
|---|---|---|
| UT99 CityIntro flyby, ~70 s | 217,221 submissions, 209,547 list calls clipped, 15 batches refused | 194,426 submissions, 0 clipped, 12 refused |
| 3DMark 2001 SE | whole run: 2,557 refused (6.4/s), 9,064,485 triangles clipped, score 678 | first 230 s, then aborted: 1,505 refused (6.5/s), 0 clipped |
| either: timeouts, resets, draws into the displayed buffer, fault dialogs | 0 | 0 |

- UT99's frames under the band show the flyby as at mask 0
  (`flyby-60s.png` in each), and it quit cleanly both times. Two earlier
  UT99 attempts are void: a guest-side 30 s input delay dropped the
  agent's connection, and a killed run left UT99 starting in its
  Recovery Mode dialog (`mask0-recovery-dialog` evidence overwritten;
  the script now clicks through it and quits UT99 by its console).
- **3DMark 2001 SE flickers in every test with the band and without it**
  (Michael, on the panel: first in Dragothic). Both runs refuse batches at
  the same rate with reason 6, the run builders refusing a vertex, which
  the band does not cause. Filed as
  [an issue](../issues/2026-11-01-gma950-3dmark2001se-flicker-refused-vertices.md).
  An earlier reading in this session, that the band run's refusals were
  the band's, was wrong: the rates are equal.
- So on this machine the band adds no refusals in any of the four
  programs: every vertex they send that the shipped path draws is within
  1,024 pixels.

## Not established

- 3DMark 2001 SE was not run to the end under the band; the band run was
  aborted once its flicker was seen, before the shipped path was known to
  flicker too.
- Frames under the band were not inspected in 3DMark 2000 or Half-Life.
- The cause of the MPREXE fault.

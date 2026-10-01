# Where a Gen3 draw submission's time goes: two thirds waiting for the GPU, and the state it reloads is three quarters repeats

Date: 2026-10-01
Machine: MICHAEL-NETBOOK, 945GSE / GMA 950, Atom N280 at 1.66 GHz, wifi
(10.0.1.254), boot 83. Full set from `aadc7dd` plus this instrument
(ABI 2026100103): DRV, VXD, HAL, SETP, ICD; every installed file
hash-verified after the WININIT rename.
Evidence: [`../probe/hal-submission-profile-2026-10-01/`](../probe/hal-submission-profile-2026-10-01/)

## Why

Recent work moved counters without moving frame rates: rectangular Mach64
textures halved Half-Life's texture churn on the Gateway at no change in
fps ([record](2026-10-01-mach64-rectangular-textures.md)). The question was
whether something common to the driver, rather than to one chip, sets the
frame rate. The Gen3 path already timed its phases; it did not say what a
submission carries, how much of its state repeats the previous one, or how
long the OpenGL ICD's draws spend in the HAL.

## The instrument (ABI 2026100103, an append)

- `I9xxStateDwords`/`I9xxPrimDwords`: dwords of state (the read flush,
  the state block and the fragment program) and of primitive per
  submission.
- `I9xxStateCompared`/`Repeats`/`ChangedDwords`: each state block compared
  with the previous submission's; identical ones counted, and the dwords
  that differ summed. Measurement only; nothing is skipped.
- `R3dDrawCycles`/`Calls`: the render interface's draw entry whole, Win16
  mutex included. `TimeD3dCalls` never saw the ICD's draws.

`hlprof.py` turns a pre/post snapshot pair into the table below.

## Half-Life `mwd5` (`hl-window.ps1`: warm-up timedemo, then snapshots around a second)

| | Direct3D (`nbprofd3d`) | OpenGL (`nbprofgl`) |
|---|---|---|
| timedemo | 29.06 fps (warm-up 25.49) | 13.34 fps (warm-up 13.83) |
| HAL draw entries, share of the window | **51.5%** | **31.4%** |
| per submission, whole | 84.3 us | 73.8 us |
| waiting for the GPU (head reaches the tail) | **52.8 us** | **41.0 us** |
| ring write (stream through the aperture) | 12.6 us | 9.7 us |
| decode (the allowlist) | 5.1 us | 4.9 us |
| build (rest of the engine draw) | 7.3 us | 8.1 us |
| front end (entry less engine draw) | 10.0 us | 9.8 us |
| submissions a second (window) | 5,772 | 4,255 |
| dwords per submission: state / primitive | 61.3 / 241.0 | 59.7 / 160.9 |
| state identical to the previous submission | **76.4%** | **80.0%** |
| dwords that differ, per compared block | 0.5 of 61.3 | 0.5 of 59.7 |
| refusals, breadcrumb timeouts, resets | 0 | 0 |

Both windows run a few seconds past the demo (the snapshot is taken at a
fixed delay), so the shares are a lower bound on the demo's own; the
per-submission figures do not depend on the window.

## What it says

**The wait is the largest single cost, and it is a design choice.** Every
submission ends with `MI_FLUSH` and a breadcrumb and the CPU spins until the
head reaches the tail: 63% of the HAL's time in Direct3D, 31% of the whole
window. The CPU and the GPU take turns. The earlier per-batch table on the
same demo (`hld3d2`, [record](2026-10-01-hl1-opengl-icd-cpu-costs.md)
evidence) puts about 10 us of each small batch's wait on the batch itself,
whatever it draws; the rest is real drawing. Asynchronous submission
overlaps the two and is what other drivers do, but it corrupted 3DMark 99
on this machine on 2026-09-29
([issue](../issues/2026-09-29-intel-gen3-async-corrupts-3dmark99.md)) and
stays off; this measurement prices it at up to the 31%, not more.

**The state reload is mostly repeats.** Three submissions in four carry the
same 61 state dwords as the one before, and those that differ differ in
half a dword on average. Emitting only changes would remove about a fifth
of the stream; on its own that is a few microseconds a submission, but it
is also what would let consecutive same-state draws become one submission
with one flush.

**The ring write is slow per dword.** 12.6 us for about 300 dwords is about
40 ns a dword: the ring lives in stolen memory reached through the GMADR
aperture, which the MTRRs leave uncacheable (no write-combining range;
`2026-09-12-intel-phase1-physical-capture.md`), and the driver has never
written an MTRR (`2026-08-28-mtrr-stage-a-inspect-only.md`). The same
applies to every CPU store through the aperture - texture uploads, locked
surfaces. Unmeasured: what write-combining would make of it.

**None of this is Intel's alone in kind.** The Mach64 path also reloads its
whole state every batch (`d3d_mach64.c`, "no redundant-state skipping until
this path has run") and is paced by its 16-entry FIFO; the front end and the
Win16 mutex per call are shared. The Gateway has no time buckets (they are
Gen3-only), so its split is not measured.

## Not established

- The OpenGL timedemo, 13.34 fps, is below this morning's 14.75 on the
  same ICD (`hlgl6`). The HAL differs by this instrument and the two
  commits since (`451a7f1`'s vertex-alpha scan, rectangular Mach64
  textures); whether either costs that much was not measured.
- How much of the GPU wait an overlap would recover; that depends on the
  async defect.
- Write-combining's effect on the ring, textures or locked surfaces.

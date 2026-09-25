# Phase 1 gate: 3DMark 99 Max on the netbook after the Gen3 move scores 669 at 1024x576, against 643 before Phase 1

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 1 gates ("Netbook: 3DMark99 at
1024x576 within noise of 643")
Machine: MICHAEL-NETBOOK (10.0.1.254:9869, agent `dev-inv2`), 945GSE /
GMA 950 rev 03, Windows 98 SE, 1024x576x16, boot 37 - the driver set of
`2026-09-26-phase1d-the-gen3-engine-reads-the-neutral-draw-and-draws-the-same.md`
(intel-gma package at b0fb7e4 plus the Gen3 move, committed as 4808fb8)
Evidence: `2026-09-26-phase1-netbook-3dmark99-score.png` (the score dialog,
captured through the agent at 09:35:30)

## Measured

3DMark 99 Max, the default "Untitled" project (every test selected, Looping
No, Repeat 1, Titles Yes), started through the agent (`exec -Detach` and
`input`) at 09:27:33 and scored from a screenshot of the Overall Score
dialog:

**669 3DMarks, 14654 CPU 3DMarks.**

The display and CPU settings panel is hidden behind the dialog in the
capture; the project was not edited, so it ran with 3DMark's defaults for
this desktop, as the earlier runs on this machine did.

## Against the reference

The plan's reference is 643, measured on 2026-09-25 by
`2026-09-25-placing-plain-textures-on-gen3.md` (commit 3385c4d) at the same
mode and stride. 669 is 4% above it.

That is not run-to-run noise: repeats of the same binary on this machine
have landed within a point of each other (244, 245, 246 at the earlier
stride, `2026-09-25-...` batch-shape and scanout records). The two drivers
differ by more than Phase 1 on the Gen3 path - between 3385c4d and 4808fb8
sit 8d17dd2 (DirectDraw blits on the Gen3 blitter) and e4d86c8 (the
application's alpha test emitted on Gen3), as well as the clipper move
(e4024a0), the neutral draw (65c5045) and the engine move (4808fb8).

**Which change raised the score is not measured.** Nothing here attributes
the 26 points; a before/after of the Phase 1 commits alone would need the
3385c4d-era HAL plus 8d17dd2 and e4d86c8 as the "before", which was not
built. The gate asks whether Phase 1 regressed the netbook, and it did not.

CPU 3DMarks (14654) sits between the two earlier readings on this machine
(14572 and 14904, `2026-09-24-...` and `2026-09-25-...` records); that
score is the CPU test and moves with whatever else the guest is doing.

## Gates

- This closes the 3DMark 99 half of the Phase 1 netbook gate.
- **Still owed for the Phase 1 record:** the 3D WinBench 98 quality suite
  on this machine (its per-test pass/fail table is the comparison, per
  `98b9bd6` and `b7ca973`); Final Reality on the 86Box ViRGE; the physical
  A8U4I5.

## Standing

3DMark was closed afterwards (Close, then Alt+F4 through `input`); the
netbook stays on the boot-37 driver set. Phase 2 has begun on the host
(non-square textures, texel alpha and the alpha test in the CPU
rasterizer); nothing of Phase 2 is on this machine yet, and the software
engine's output is unchanged by construction until Direct3D's `d3d_soft.c`
asks for those features.

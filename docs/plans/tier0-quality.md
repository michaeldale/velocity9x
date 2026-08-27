# Tier-0 quality: write-combining, hardware cursor, synthetic vblank, DOS-box guard

Date: 2026-08-28

Status: proposed

Roadmap Track D (`family-structure-and-next-d3d-roadmap.md`). Four items that
came out of surveying JHRobotics' vmdisp9x/vmhal9x for capabilities Velocity9x
lacks. Those drivers target virtual GPUs, so most of their surface does not
transfer — but these four are real-hardware wins, and every one benefits the
tier-0 path that all families, the upcoming 3dfx included, stand on. House
rule restated: ideas only; the implementations are written from the Intel
SDM, chip documentation, and this repository's own interfaces, with no
third-party driver source copied.

Ordered by value. Each item is independently shippable.

## D1. MTRR write-combining on the linear framebuffer

**Stage A implemented 2026-08-28** — the registers are read, the decision is
made by host-tested policy, and nothing is written; see
[the decision record](../decisions/2026-08-28-mtrr-stage-a-inspect-only.md).
Stage B (the write, behind a SYSTEM.INI kill switch) is gated on the `Mtrr=`
evidence Stage A collects, and that decision record lists what it needs. The
scope below is the original item; the split into two stages is the change.

Tier-0 draws with the CPU through an LFB that is uncached by default, so
every GDI and DirectDraw operation pays full uncached-write cost. Marking the
aperture write-combining via a variable-range MTRR is the standard fix and
likely the largest free performance win available to the vbe family.

Scope:

- Mini-VDD only, therefore Win98-only by the same Code-24 constraint that
  scopes the VBE collection (Win95 names no mini-VDD by design; it keeps
  uncached writes).
- At `Device_Init`, after the VBE collection has established the aperture
  base and size: CPUID gates first (MTRR feature bit; vendor families known
  to implement it faithfully), then program one free variable-range MTRR to
  WC over the aperture, size rounded per the SDM's alignment rules. Record
  what was done (or why not) in the diagnostics INI — measured evidence, as
  always, decides whether it stays.
- Registry kill switch (`MtrrWriteCombine=0`), because a wrong memory type
  on real silicon shows up as corruption that looks like a driver defect.
  The gdi-accel poison-report pattern is the model.
- Out of scope: AMD fixed-range/K6 MWTC variants, and any PAT use. One
  mechanism, gated, measured.

Measure: CrystalMark GDI/DDraw numbers on the netbook (Atom, vbe family)
before/after — the machine most starved by uncached writes — and BARRY for
the regression direction. Exit gate: measured improvement recorded in a
decision doc; no mode-matrix or scanout regressions on either.

Risk note: MTRRs are system-wide state. The mini-VDD must verify the range is
not already covered by an existing MTRR (a BIOS sometimes pre-maps it) and
must not shrink or overwrite ranges it did not create.

## D2. Hardware cursor

Everything today rides the DIB Engine's software cursor: an exclusion
rectangle serialized against drawing — the same exclusion the GDI-accel work
already has to respect — and a repaint on every move. Every family's silicon
has a hardware cursor (S3 Trio/ViRGE: the CR45-family HWGC; Voodoo3: video
overlay cursor; even VBE has no path, which is fine — see gating).

Scope:

- A per-chip `cursor` hook set on `V9X_HW16_DEVICE` (nullable, like the other
  hooks: NULL keeps the DIB Engine cursor, which is the tier-0 answer and the
  vbe family forever).
- First implementation: S3 HWGC on the Trio64, where BARRY plus two emulated
  guests give the cheapest verify loop; ViRGE follows as data.
- The 16-bit driver's `SetCursor`/`MoveCursor`/`CheckCursor` DDI exports move
  from unconditional DIB forwards to the gate-then-decline pattern
  gdi-accel established.

Exit gate: cursor visible and artifact-free through the mode matrix and a
GDI-accel run on BARRY (the exclusion interaction is the point), software
path unchanged for hook-less families.

## D3. Synthetic vblank for tier-0

Tier-0 publishes no `CAP_VBLANK`, so `WaitForVerticalBlank` and
`DDGBS_ISBLTDONE`-adjacent timing on the vbe family fall to whatever DDRAW
emulates. A timer-derived beam position — refresh period from the mode's
declared refresh rate, phase free-running — gives tier-0 games a plausible
vblank without touching hardware.

Scope: 32-bit HAL, one shared module; used only when the engine descriptor
carries no vblank capability. Honest by construction: it is a pacing aid,
not a tear-free guarantee, and the diagnostics say `Vblank=synthetic`.
Exit gate: a vblank-waiting title (Doom95 is already in the compatibility
set) paces correctly on the vbe guest instead of free-running.

## D4. DOS-box mode-set guard, and a windowed-DOS test

vmdisp9x deliberately restricts INT 10h handling so a windowed DOS box
cannot confuse the BIOS/driver mode state. Velocity9x has never tested
windowed DOS boxes against the dynamic mode pipeline — the mini-VDD collects
at boot, but a DOS box's INT 10h traffic flows through the VDD at runtime.

Scope, deliberately test-first: add a windowed-DOS-box step to the VM mode
matrix (open a DOS window, run a mode-touching DOS program, close, assert
desktop intact via the scanout check). Only if that fails does a guard get
designed — the failure shape decides where it lives. Exit gate: the test in
the matrix, green or a filed issue.

## Sequencing

D1 first (biggest win, smallest surface), then D4's test (cheap, and the
answer shapes confidence in everything else), then D2, then D3. Nothing here
blocks the 3dfx tier-0 plan; D1 and D4 land before it ships its packages so
the new family inherits them.

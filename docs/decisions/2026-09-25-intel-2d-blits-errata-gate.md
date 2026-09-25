# Intel Gen3: hardware 2D blits are authorised on the terms of runtime 3D

Date: 2026-09-25
Status: accepted. Decided by Michael Dale ("do blit", on the evidence below),
as the one-sentence decision `docs\plans\intel-gen3-directdraw-blits.md`
requires before the first armed boot. Extends, and does not inherit,
`2026-09-15-intel-phase5-errata-gate.md` and its sustained-3D amendment.

## The decision

DirectDraw fills and copies may run on the 945GSE blitter through the ring,
on the same preconditions as runtime 3D: `8086:27AE` rev 03 exactly, a
scratch install, AC power, recoverable from DOS with `V9X3D OFF`.

## Why it is not inherited

The house rule is that a new class of workload does not take a previous
class's authorisation. A 2D blit is the Phase 4 workload - one blit, the CPU
polling - at an application's rate, into memory the application also
writes through the aperture between blits. That is a denser interleave of
CPU and GPU access to the same pages than the 3D path makes, which is
erratum 12's own wording ("an incorrect internal-buffer flush for a
particular sequence of processor and integrated-graphics memory accesses";
Intel 309220-0132, no published trigger, no fix on A3).

## Why it is accepted

- **The CPU already does these accesses.** Every Blt today is a CPU copy
  or fill through the uncached aperture into the same pages. The change is
  which engine executes it, not which memory is touched.
- **The blitter already runs at this rate.** Every 3D batch ends in a
  breadcrumb that is itself an `XY_COLOR_BLT`: 5,812,779 of them in one
  Half-Life session on boot 27 (`V9XSNA7.INI`: `BreadcrumbSubmits`), with
  `BreadcrumbTimeouts=0`, `EngineResets=0`.
- **The cost of not doing it was measured.** The same session had 11,237
  CPU copy blits averaging about 53 million TSC cycles each - some 32 ms at
  the netbook's 1.63 GHz, render drain included - which is more time than
  all of its Direct3D calls together (`TimeBltCopy*` against
  `TimeD3dCalls*`). Half-Life presents every frame with one of them.

## What this does not establish

Anything about erratum 12's boundary. Nothing measured says where it is;
this decision accepts the exposure, it does not bound it. A hang during a
DirectDraw session after this build is the first thing to suspect.

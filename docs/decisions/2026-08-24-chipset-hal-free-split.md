# Making `src/chipsets` HAL-free, ahead of a future NT driver

Status: Noted, not scheduled — 2026-08-24

Date: 2026-08-24

## Why this is written down

The question came up of what it would take to support Windows 2000/XP. The
short answer is that it is a second driver, not a feature: Win9x uses a 16-bit
`display16` + DIB-engine `display32` pair plus a VxD (`minivdd32`), and NT uses
XPDM — a `videoprt.sys` miniport plus a GDI display DLL built on `Drv*`/`Eng*`.
Different entry points, different memory model, different DDK, different INF
layout, plus driver signing. The DirectDraw HAL likewise moves from the 9x
DDHAL to `DrvGetDirectDrawInfo` and kernel-side `Dd*` callbacks.

What *does* port is the part that took the longest to get right: the per-chip
register programming in `src/chipsets` — CRTC/PLL setup, mode tables, aperture
mapping, blitter command construction. That is ordinary C and is worth roughly
30-40% of an NT driver's content.

Order of work, for the record: finish the current VBE work, then VBE 3, then
VBE/AF, then look at Glide/OpenGL/Direct3D on 3dfx. An NT driver comes after
all of that, if at all.

## The proposal, in one line

Restructure `src/chipsets` so it is a self-contained library that knows about
registers and modes and nothing about the Win9x driver environment — no DDI
types, no DIB engine, no VxD services, no `display16`/`display32` headers — so
that a future NT miniport can link the same code.

## Should it happen on the VBE branch?

No.

- The VBE branch's job is dynamic mode enumeration. A chipset-wide interface
  refactor shares no code with it, and would make the branch's diff impossible
  to review as VBE work.
- The VBE work is itself likely to *change* what the chipset boundary needs to
  expose (mode enumeration is exactly the surface in question). Fixing the
  boundary now, then reopening it in the next two VBE phases, is wasted motion.
- The refactor's payoff is years out on the current ordering. Its cost — a
  large mechanical change across four chipset families, each with a different
  amount of physical-hardware verification behind it — is paid immediately, and
  every regression it introduces lands in targets that are hard to retest.

The right moment is after VBE/AF, once mode enumeration has stopped moving, and
before any 3dfx acceleration work adds a fifth family to the blast radius.

## What to look at when it is scheduled

Nothing here has been measured yet — this is the list of questions, not answers:

- Which headers under `src/chipsets/{s3,ati,matrox,generic}` currently pull in
  Win9x-only types, and how deep the leakage goes.
- Whether the chipset code calls back into `src/common` for logging
  (`log.c`), PCI/resource discovery (`resources.c`), and mode bookkeeping
  (`mode.c`, `vbe_*.c`), and whether those need to become injected callbacks
  rather than direct calls.
- The int 10h problem: dynamic VBE enumeration depends on real-mode calls that
  an NT miniport can only make through `VideoPortInt10`, and only for the boot
  device. Any HAL-free split has to keep VBE behind an interface the NT side
  can implement differently, or the "portable" library is not portable.
- Whether the split is worth doing purely for its own sake — testability of the
  chipset code in isolation on the host — independent of the NT question.

## Decision

Do not start this now, on this branch or any other. Revisit after VBE/AF.

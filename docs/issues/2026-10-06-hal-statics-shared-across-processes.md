# The HAL's static data is shared by every DirectDraw process

Status: open
Filed: 2026-10-06
Found in: A8U4I5 (Rage XL PCI, Windows 98 SE, DirectX 9.0c), build `6ff23ee`,
while capturing DrawPrimitives2 calls for Part C of
`docs/plans/ddi6-drawprimitives2.md`.

## What was seen

The DrawPrimitives2 capture (`src/display32/d3d/d3d_dp2_ring.c`) keeps its
state in file-scope statics of `V9XHAL.DLL`. After one Half-Life process
had written its capture (`v9x_dp2_ring_phase` 3) and exited, the next
Half-Life process on the same boot found the phase still at 3 and never
armed: the second run's `V9XDP2R.BIN` was byte-identical to the first
(`xl-V9XDP2R-2.BIN` and `-3.BIN` in the session scratchpad, same skip count
122,124). A Win32 DLL's data is normally per process; this one behaved as
shared. The likely reason is that DirectDraw on Windows 98 loads the HAL
DLL into the shared arena above 2 GB, as it does `DDRAW.DLL` itself, but
that is inferred, not shown.

The capture was changed to re-arm per process (`v9x_dp2_ring_rearm`, keyed
on `GetCurrentProcessId`), which worked; the general question was left.

## Why it matters

Anything that is per process but kept in a HAL static is then visible to,
and usable by, every other DirectDraw process:

- memory from `VirtualAlloc` and pointers the runtime hands over belong to
  one process's address space;
- the DrawPrimitives2 probe cache (`v9x_d3d_dp2_probed`,
  `v9x_d3d_dp2_probed_rstates` in `src/display32/d3d/d3d_core.c`) records
  that a pointer passed `IsBadReadPtr`/`IsBadWritePtr` and skips the check
  next time. It is cleared at every context creation and destruction, which
  is why one Direct3D program at a time is safe;
- the pending DrawPrimitives2 run (`v9x_d3d_dp2_run`) holds copies, not
  pointers, but its batch sink names a context;
- the colour-key and alpha-mask tables, and any other table keyed by
  surface pointer, would compare pointers from different address spaces.

## Not known

- Whether the sharing is real or something else kept the phase (the
  experiment that would settle it: a counter static reported through the
  diag snapshot from two processes).
- Which statics hold per-process pointers; nobody has listed them.
- Whether two Direct3D programs at once (or one Direct3D program and
  DxDiag) misbehave today. Never tested.

## Next

1. Prove or disprove the sharing with the counter experiment.
2. List every HAL static that holds a process-specific pointer or handle,
   and say for each whether it is invalidated per process.
3. Fix by keying on the process id, forgetting on context creation, or
   per-process storage; measure two concurrent Direct3D processes before
   and after.

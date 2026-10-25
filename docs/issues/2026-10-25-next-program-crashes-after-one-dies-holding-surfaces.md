# A program that dies holding surfaces crashes the next DirectDraw program

Date: 2026-10-25. Reported by Michael on the Rage 128 ("switch between
3d programs ... the program either crashes or has graphical issues,
reboot fixes it"); reproduced by V9XD32 T14 on the GMA 950 netbook and
on A8U4I5. Status: open. Present in 0.28.0 (netbook boot 244), so not a
0.29.0 regression. Not fixed.

## Reproduction

`V9XD32 /ddi6` (T14): a second copy of V9XD32 sets 640x480, draws,
flips and exits without releasing; this copy then opens DirectDraw and
dies in its first `CreateSurface` (the primary flip chain). On the
netbook the process ends silently; on A8U4I5 with a page-fault dialog.

| The child leaves | Next program | Machine |
|---|---|---|
| everything, mid-scene | dies | netbook, A8U4I5 |
| everything, scene ended (`/t14endscene`) | dies | netbook |
| surfaces and DirectDraw; device released (`/t14release3d`) | dies | netbook |
| only the display mode (`/t14nomode`) | PASS | netbook |
| nothing (`/t14clean`) | PASS | netbook |
| everything, then 5 s before the reopen (`/t14wait`) | dies | netbook |
| everything, under ATI's own driver | reopens and draws | A8U4I5 |
| everything, 0.28.0 | dies | netbook |

## Where it dies

The HAL's fault filter now writes the faulting thread's registers, stack
and EBP chain after `V9XTRACE.INI`'s dump (`[Velocity9xFaultContext]`).
Netbook boot 243:

- `EIP` `BAAD3896`: DDRAW.DLL (base `BAAA0000`) RVA `33896`,
  `test [eax], ecx` with `EAX` = `000000A0`.
- The routine walks the driver object's VIDMEM array (`ESI`), and for
  each heap reads `lpHeap` (`[esi+14h]`) plus `A0h` or `ACh`: `lpHeap`
  is null. DirectDraw is allocating against a heap it has freed.
- Callers, all DDRAW.DLL: RVAs `2B6A4`, `2D5DB`, `2D727`, `2DC19`,
  `2DDE7`, `2DF59`; then V9XD32 at `0040400D`, its `CreateSurface`.

The HAL's trace ring around it (netbook): DDHELP's cleanup of the child
(`FlipToGDISurface`, one `DestroySurface`, `D3dContextDestroyAll`, all
returning), then `Dd16CreateObject` with reset, `GetDriverInfo`, then in
the next program `SetExclusiveMode` 0, `Dd16DestroyDriver`,
`SetExclusiveMode` 1, `CanCreateSurface`, `CreateSurface` - both
answered - and the fault. After a clean child, the next program's
`DirectDrawCreate` makes a driver object of its own (`Dd16CreateObject`)
and its `SetDisplayMode` refreshes it; after a dead one, it attaches to
the dead program's driver object, which DirectDraw then destroys under
it, and nothing refreshes it.

## Hypotheses killed

- DDHELP still cleaning up: a 5 s wait changes nothing.
- The driver's `DestroyDriver` dropping `SetInfo`, so a later refresh did
  nothing: kept across `DestroyDriver`, it still dies, and no refresh is
  asked for after the destroy anyway.
- Both driver objects sharing one 16-bit VIDMEM copy: a separate copy
  per object changes nothing.
- The Rage 128 or Intel engines: identical on both; the command engine
  is not involved (`/t14release3d` destroys the context cleanly).

## Not known

- Why ATI's driver survives the same sequence. A guess, not checked:
  ATI's 32-bit `CreateSurface` places surfaces itself, so DirectDraw
  never walks its heap list for them.
- Whether DirectDraw's destroy of the dead program's driver object
  under a live one is something a driver can avoid, or only survive.

## Next

Either place flip chains and offscreen surfaces from the HAL's own
allocator in `CreateSurface` (what ATI is guessed to do; large), or find
what lets DirectDraw keep the dead object alive or rebuild the live
one's heaps. Disassembling DDRAW.DLL RVA `2D5DB`'s routine against the
DDK's `ddrawi.h` would say which.

# A program that dies holding surfaces crashes the next DirectDraw program

Date: 2026-10-25. Reported by Michael on the Rage 128 ("switch between
3d programs ... the program either crashes or has graphical issues,
reboot fixes it"); reproduced by V9XD32 T14 on the GMA 950 netbook and
on A8U4I5. Status: cause found; fix measured on the netbook, not yet
committed or run on A8U4I5 (see "Mechanism" and "Fix"). Present in 0.28.0
(netbook boot 244), so not a 0.29.0 regression.

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

## Not known (as first written; answered under "Mechanism")

- Why ATI's driver survives the same sequence. A guess, not checked:
  ATI's 32-bit `CreateSurface` places surfaces itself, so DirectDraw
  never walks its heap list for them.
- Whether DirectDraw's destroy of the dead program's driver object
  under a live one is something a driver can avoid, or only survive:
  avoid, by answering the reset's caps query consistently.

## Mechanism

Read from the netbook's DDRAW.DLL (4.07.00.0700, PE stamp `40EE6AFA`,
base `BAAA0000`) with `wdis`, against the 98 DDK's `ddrawi.h`, then
measured with trace pushes that are no longer in the tree (below). RVAs
are DDRAW.DLL's. Structure offsets: `DDRAWI_DIRECTDRAW_GBL` `dwRefCnt`
+0h, `vmiData` +3A4h so `dwNumHeaps` +3ECh and `pvmList` +3F0h,
`lpExclusiveOwner` +3F8h, `lpDDCBtmp` +170h; `VIDMEM` is 18h bytes with
`lpHeap` at +14h; `DDRAWI_DIRECTDRAW_LCL` `lpGbl` +4h, `dwLocalRefCnt`
+10h, `dwProcessId` +14h, and (DX7, not in the 98 DDK) the next local at
+70h; `D3DHAL_CALLBACKS3.DrawPrimitives2` +14h.

1. DDRAW's DllMain registers RVA `38506` with DDHELP (RVA `17763`). When
   a program dies, DDHELP calls it: it marks DDHELP as acting for the dead
   process (globals `BAAF8394`/`BAAF8390`) and runs RVA `19E50`, which
   destroys the dead process's surfaces, palettes and clippers and then
   calls `IDirectDraw::Release` (RVA `3D6AD`) on each of its interfaces.
   Measured: the HAL's `GetCurrentProcessId` during that teardown
   (`FFCFED87`, boot 248) is not the child's (its LCL records `FFFCFD67`,
   as does `D3dContextDestroyAll`); it is DDHELP's.
2. The last release restores the display mode. The 16-bit driver
   re-registers (`Dd16CreateObject` with reset) and DDRAW rebuilds the
   live driver object in place: RVA `1BC66` with reset copies the object,
   frees every heap of the old one (RVA `1BF12`, `lpHeap` set to 0 by RVA
   `3360B`), then re-fetches the caps. It asks for `GUID_D3DExtendedCaps`
   again (RVA `1C316`) and, because the object holds a `DrawPrimitives2`
   callback (GBL+638h is the `D3DHAL_CALLBACKS3` copy made at creation,
   RVA `1B565`; +14h tested at RVA `1C359`), refuses an answer shorter than
   54h bytes (RVA `1C37B`). The reset fails (RVA `1CA67`) after the heaps
   are gone and before new ones are made (`pvmList` still set, every
   `lpHeap` 0).
3. Why the answer is short: the HAL decides DDI 5 or 6 per calling process
   (`v9x_d3d_dp2_for_process`), and DDHELP.EXE is not listed and has no
   D3D8, so it gets DDI 5 and the 24h-byte form. Measured, boot 248:
   `GUID_D3DExtendedCaps` answered with `dwExpectedSize` 74h,
   `dwActualSize` 24h, DDI 5 form, in DDHELP, for an object V9XD32 made at
   DDI 6. `GUID_D3DCallbacks3` is asked only while an object is made (RVA
   `1B9B5`, in the routine RVA `1BDAE` calls only when there is no object
   to reset), so the object keeps its DDI 6 callbacks while its caps are
   re-asked at DDI 5.
4. `Release` carries on: `SetExclusiveMode(0)`, then with the global
   count 0 the HAL `DestroyDriver` (RVA `3D929`, the only call of it),
   then the heap loop RVA `3D9D6`, which does not check `lpHeap`. RVA
   `3360B` calls `VidMemFini(NULL)` (RVA `33398`), which reads `[eax]` at
   once: an access violation. The `__try` in `Release` was closed at RVA
   `3D6ED`, so the fault leaves the function; the process-detach path
   swallows it. Skipped with it: freeing `pvmList` and the object (RVA
   `3DA00`, `3DACD`) and unlinking and freeing the dead LCL (RVA `3DB4F`).
   Measured, boot 247: at the parent's `SetExclusiveMode(1)` DDRAW's LCL
   list (head at `BAAF839C`) still holds the dead child's LCL (refcount 0)
   pointing at the destroyed object.
5. The next program's `DirectDrawCreate` (RVA `1CDD0`) walks that list
   (RVA `1D37B`) for an LCL whose object names the same device, finds the
   dead one and reuses its object: no `Dd16CreateObject`, `dwRefCnt` 0 to
   1. `CreateSurface` then allocates through RVA `33802`, which reads
   `lpHeap`+A0h of the first heap: the fault at RVA `33896`, `EAX` = A0h.

So DirectDraw does not destroy a live program's object on purpose: the
object is destroyed by the dead program's own release, and only becomes
"live" again because a fault in that release left it reachable. The
driver's part is the short `D3DExtendedCaps` answer in step 3. The clean
child passes because it releases in its own process, which gets DDI 6.
`/t14nomode` passes because no mode is restored, so there is no reset.

What a driver answer cannot change: DDRAW frees the heaps before it
knows the reset will succeed, and its release path frees heaps without a
null check. Any other reason for a reset to fail after RVA `1BF12`
(allocation failures, mostly) would end the same way.

Measurements (netbook, V9XD32 `/ddi6`, trace pushes in the HAL):

| Boot | What was added | What it showed |
|---|---|---|
| 246 | pid, object, `dwRefCnt`, owner, `lpHeap` of heap 0 at `SetExclusiveMode`, `FlipToGDISurface`, `CanCreateSurface`; `dwRefCnt` at `DestroyDriver` | at the teardown's `SetExclusiveMode(0)` the count is 0 and heap 0 already freed, before `DestroyDriver`; the next program gets the same object (`83D5B23C`) with count 1 |
| 247 | DDRAW's LCL list at `SetExclusiveMode` | the dead child's LCL, count 0, still listed after it is gone |
| 248 | the `GUID_D3DExtendedCaps` answer | DDI 5 form, 24h bytes, in the teardown |

The diagnostic patch and the three trace files are kept outside the tree
in the session notes; the pushes read DDRAW internals at fixed addresses
and were not meant to ship.

Hypotheses killed here:

- The teardown runs in the dying child: the process id says DDHELP.
- The next program attaches to a freed and reallocated object: the
  object was never freed; the release faulted before freeing it.
- DirectDraw destroys the object under a live program it knows about:
  the live program attaches after the destroy, through a dead LCL.

## Fix

`src/display32/d3d/d3d_core.c`: the DDI level is decided when DDRAW asks
for `GUID_D3DCallbacks3` (it asks only while making an object), kept in a
HAL static (every HAL section is shared, as the object is), and every
later `GetDriverInfo` answer and the publish use it. A reset from DDHELP,
or from any process attached to an object another process made, now
describes the object at the level it was made with.

Measured on the netbook with that change alone, every runtime file
hash-checked against the package: boot 251, `V9XD32 /ddi6` and
`/ddi5` both `Result=PASS`, `T14Result=PASS`, `T15Result=PASS`. Before
it, boots 246-248, T14 died in `CreateSurface` every time.

A8U4I5 (Rage 128), current V9XD32 pushed first. Boot 616, its installed
set (HAL `9b76e8b3-dirty`), `/ddi6`: T14 died in `CreateSurface`, fault
at `BAAD3896`, the same DDRAW RVA as the netbook. Boot 617, the `ati`
package from the same tree as the netbook's, all runtime files
hash-checked: `/ddi6` `Result=PASS` with T14 and T15 PASS; `/ddi5` run
three times, T14 PASS every time. The first `/ddi5` run failed T15
(`T15HeapBad=1`, first bad word at offset D685C0h of the 32 MiB heap
pattern, so `Result=FAIL`); the next two passed it. T15 checks memory the
driver does not own and was never reached on A8U4I5 before, because T14
died first.

T15 control, `/ddi5 /t14nomode` three times in a row on each build
(`/t14nomode` reaches T15 without the fix too):

| Build | Boot | Run 1 | Runs 2, 3 |
|---|---|---|---|
| with the fix | 617 | FAIL: heap, offset A295C0h | PASS |
| without (`6bcfac9a`) | 618 | FAIL: shared mapping, offset 785C0h | PASS |

So the T15 corruption predates the fix and is not this issue. Every
failure so far is the first run of a batch, and every first bad word is
at page offset 5C0h (D685C0h, A295C0h, 785C0h): one writer, landing in
whatever page it is handed. Not investigated further here; it wants its
own issue.

A8U4I5 left on `5dcbdb32`'s `ati` package from boot 619.

Not done: the `/t14*` control variants with the fix, and ATI's driver. Why ATI's driver survives is still not checked; the mechanism
above predicts that any driver whose `GUID_D3DExtendedCaps` answer does
not depend on the calling process does, and makes the earlier guess
(ATI places surfaces itself) unnecessary.

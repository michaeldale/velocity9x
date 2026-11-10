# A program that dies holding surfaces crashes the next DirectDraw program

Status, 2026-11-10: fixed. The fix below was committed as `5dcbdb32` the
same day and has shipped since 0.30.0; T14 has passed on committed builds
on the netbook and A8U4I5 since. The status line that follows is the
original and went stale. See "2026-11-10: status check and an 86Box run".

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

## 2026-11-10: status check and an 86Box run

The README still listed this as a known issue on every card, and
docs/STATUS.md and the issue index repeated this record's opening line
("not yet committed"). Git says otherwise:

- `5dcbdb32` (2026-10-25) is the fix described above. It is on main and
  in every tag from v0.30.0. `src/display32/d3d/d3d_core.c` still decides
  the level per object (`v9x_d3d_object_ddi6`, `v9x_d3d_ddi_for_object`);
  the DDI 8 work extended it (`v9x_d3d_get_driver_info2` latches DDI 8 the
  same way) and did not undo it.
- The 0.30.0 changelog does not mention the fix; the 0.29.0 known-issue
  entry was never withdrawn.
- T14 on committed builds: netbook, 0.30.0 checks, `/ddi6` and `/ddi5`
  PASS with the default child
  ([record](../decisions/2026-10-26-0.30.0-release-checks.md)); A8U4I5,
  0.32.0 checks, PASS at `/ddi6` and `/ddi5`, and the `-t14nomode` in the
  report names did nothing, so the default child ran
  ([record](../decisions/2026-10-27-0.32.0-release-checks-ati-ironlake.md)).
  Since then V9XD32, T14 included, is in every release check. That is the
  exit criterion docs/STATUS.md set for this row.

Then an attempt to reproduce it on the 86Box ViRGE/DX guest (Win86SE,
`virge_dx_pci`, Win98 SE, port 9869, agent updated 0.9.1 to 0.16.0 first),
`s3` family, 1024x768x16 desktop, V9XD32 `/bpp16 /ddi6`. Reports in
[`../probe/86box-virge-dead-program-2026-11-10/`](../probe/86box-virge-dead-program-2026-11-10/).

| Boot | Driver set | Probe | Dying program | Next program |
|---|---|---|---|---|
| 674 | the guest's own, an 0.15.x-era set | `852e8a1d` | T14 child | T14 PASS, but `RunDdi=unknown`: the probe could not read that HAL's counters, so the DDI level is unknown. Not counted. |
| 675 | `6bcfac9a` (the fix's parent), WININIT rename, hashes checked | `6bcfac9a` | T14 child, mid-scene, `ExitProcess` | opens DirectDraw, flip chain, device, draws, reads back. `RunDdi=6`, 248 DrawPrimitives2 calls |
| 675 | same | same | V9XD32 killed with the agent's `kill` (`TerminateProcess`) at T13, exclusive flip chain and device held | runs to the end; every result line equals the undisturbed run's |
| 676 | `852e8a1d` (main) | `852e8a1d` | T14 child | T14 PASS (`DITHERED`), `RunDdi=6`, 259 DrawPrimitives2 calls |
| 676 | same | same | killed at T13, as above | runs to the end; every result line equals the undisturbed run's |

The `6bcfac9a` probe reports T14 `FAIL` with 374 mismatches. That is T1's
count, the ViRGE's 16-bit ordered dither, which that probe did not yet
classify (`DITHERED` came on 2026-11-05,
[record](../decisions/2026-11-04-virge-dx-silicon-3d-and-mip-filter-swap.md));
the next program did not die. Main's two failures on this guest are T20
(`T20Ok=0x8A0`) and T21 (`T21Ok=0x8`), not looked into here.

So on this guest the defect does not reproduce even without the fix: a
DDI 6 program that dies, by `ExitProcess` or by `TerminateProcess`, does
not break the next one. The guest runs DirectX 6.1 (DDRAW.DLL
4.06.03.0518; `HKLM\Software\Microsoft\DirectX` `Version`), not the
4.07.00.0700 DDRAW.DLL read above; the netbook has DirectX 7 and A8U4I5
DirectX 8 or later. The likely difference is that DirectX 6.1's reset
does not re-ask `GUID_D3DExtendedCaps` or does not refuse the short
answer; that was not read from its DDRAW.DLL, so it is a guess. The 86Box
guest is therefore not a regression test for this defect as it stands. A
DirectX 8.0 redistributable is on the host (`DX80ENG.EXE`); installing it
on the guest would change the VM for good and was not done.

Win86SE was left on main's set (`852e8a1d`), boot 676, with V9XD32 at
`C:\V9XD32.EXE`. Its previous set is saved outside the tree in the session
notes.

### The same guest with DirectX 8.0 (2026-11-10, later)

Michael asked for DirectX 8.0 on the guest. Cold profile backup first
(`86Box VMs\Velocity9x Backups\Win86SE-pre-velocity9x-20261110-172157`,
DirectX 6.1, main's set), then `DX80ENG.EXE /Q /T:C:\DX8\EX /C` and
`DXSETUP /silent` (exit 1, its own WININIT with `NUL=` lines, which went
through on an agent `reboot`). Boot 678: DirectX `Version` 4.08.00.0400,
DDRAW.DLL 344,064 bytes (299,008 before), D3D8.DLL present, and V9XD32's
T16 (DirectX 7) now runs and passes. Same switches as above; files with
the `dx8-` prefix in the same folder.

| Boot | Driver set and probe | Dying program | Next program |
|---|---|---|---|
| 678 | `852e8a1d` (main) | T14 child | T14 PASS, `RunDdi=6`, 264 DrawPrimitives2 calls; `Failures=2`, T20 and T21 as before |
| 678 | same | killed (`TerminateProcess`) at T13 | runs to the end; result lines equal the undisturbed run's |
| 679 | `6bcfac9a` (pre-fix), hashes checked | T14 child | creates surfaces and device, draws, reads back; T14 `FAIL` is the 374 dithered pixels again (`Failures=0xA`: 7 culling, 3 dither, as that probe judged the ViRGE) |
| 679 | same | V9XD32 `/child /ddi6` run alone, then a V9XTRACE snapshot | the next V9XD32 runs to T13 normally (killed there by design) |
| 679 | same | killed at T13 | runs to the end; result lines equal the undisturbed run's |

The snapshot after the lone child (`dx8-prefix-child-V9XSNAP.INI`, ring
entries 429-445) holds the sequence this record describes from the
netbook: `FlipToGDISurface`, `DestroySurface`, `D3dContextDestroyAll`,
`Dd16CreateObject` with reset, one `GetDriverInfo` for `7DE41F80`
(`GUID_D3DExtendedCaps`), `SetExclusiveMode(0)`, `Dd16DestroyDriver`. So
the reset and the re-asked caps happen under DirectX 8.0 too. The ring
does not record the answer's size or whether DDRAW refused it; the trace
pushes that showed that on the netbook are not in the tree.

So the 86Box ViRGE guest does not reproduce the defect with DirectX 6.1
or 8.0, before or after the fix. It reproduced on the netbook (DirectX 7,
DDRAW 4.07.00.0700) and A8U4I5. Not established: whether DirectX 8.0's
DDRAW accepts the short answer, or something on this guest keeps the
reset from failing; telling them apart needs DX8's DDRAW.DLL read the
way DX7's was above, or the size pushes back in a scratch HAL. The fix
is not in question: it removes the per-process difference either way.

Win86SE left on main's set (`852e8a1d`), DirectX 8.0, boot 680, V9XD32
from main at `C:\V9XD32.EXE`.

### Haswell 4570, real hardware (2026-11-10, later)

W9QI5A83 (i5-4570, HD 4600, intel-gma), Windows 98 SE with DirectX
9.0c (4.09.00.0904), agent 0.13.1, boots 197-198. HAL built from
`24ecd14e` (0.44.0, so with the fix); V9XD32 from main (`852e8a1d`,
`/bpp16 /ddi6`) as `C:\V9XD32M.EXE`. No pre-fix build was run here.
Files with the `hsw-` prefix in the same folder.

| Boot | Dying program | Next program |
|---|---|---|
| 197 | T14 child (undisturbed run) | `Result=PASS`, `Failures=0`, `RunDdi=6` (read live; the saved copy was cut by the agent's bounded `cat` and is not kept) |
| 197 | V9XD32 killed twice through the agent | `Result=PASS` both times; neither kill held surfaces (below) |
| 197 | 3DMark 2000 default benchmark, killed 45 s in (fullscreen 1024x768) | never started: see below |
| 198 | the same, killed 45 s in | V9XD32 `Result=PASS`, `Failures=0` |
| 198 | the same again, with the boot 197 calls after the kill | V9XD32 `Result=PASS`, `Failures=0` |

V9XD32 runs end to end in under 4 s on this machine and each agent call
over its wifi takes most of a second, so both V9XD32 kills landed at
`12-t14-child`, after the parent had released its own surfaces. They
show only that a parent killed while waiting on its child does no harm.
The 3DMark kills are the real test: a DirectX 7 program holding an
exclusive flip chain and a device at DDI 7.

At boot 197 the first 3DMark kill succeeded, and a window list 4 s
later showed the 800x600 desktop back with 3DMark gone. The next call,
a screenshot, timed out, and the machine then answered neither ping
nor the agent for over 6 minutes, until Michael power-cycled it. What
the screen showed then was not recorded. Boot 198 left nothing about
it in V9XDDT.TXT, V9XD7.LOG or BOOTLOG.TXT. Two repeats at boot 198,
the second with the same calls in the same order, did not reproduce
it. Whether boot 197 was a machine hang or the wifi key dropping is
not established; the wifi utility on this box has stolen focus and
dropped transfers before.

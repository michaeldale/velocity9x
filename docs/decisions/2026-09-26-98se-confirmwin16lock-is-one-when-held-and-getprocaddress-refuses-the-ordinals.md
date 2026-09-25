# On 98SE, _ConfirmWin16Lock returns 1 exactly when the calling thread holds the mutex, and GetProcAddress refuses the KERNEL32 ordinals that name it

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 0.2 (the calibration the second
half needs) and Phase 3 (the HAL's route to the mutex)
Instrument: `V9XW16L.EXE` (`tools/diag/win16lock_probe_win32.c`, built by
`scripts/build-win16lock-probe.ps1`), build `6212282-dirty`
Evidence: `2026-09-26-98se-w16l-virge-9869.ini`,
`2026-09-26-98se-w16l-fastd3d-9878.ini`

Run once on each of two 86Box Windows 98 SE guests, `Win86SE` (port 9869,
ViRGE/DX, Velocity9x driver) and `Win98SE-Fast-D3D` (port 9878, Voodoo3 on
the vbe package), from the agent's `exec` on the agent's thread. Both runs
exited 0 with `Result=PASS`; nothing crashed, so the exception filter's
record was never written.

## Why

The OpenGL plan has the HAL take the Win16 mutex itself, through KERNEL32
ordinals 93/97/98, and has a trace build call `_ConfirmWin16Lock` (#96)
inside DirectDraw's callbacks to measure whether DirectDraw holds the mutex
there. Three things had to be known first: whether the ordinals resolve at
all from a Win32 process, and by which route; what `_ConfirmWin16Lock`
returns, since the sources disagree on whether it is a boolean, a count, or
an assertion; and whether calling it while *not* holding the lock is safe.
Putting an untested call into a shared-arena DLL that every DirectDraw
process loads is the wrong place to find out.

## Measured, identically on both guests

**Resolution.** `GetModuleHandleA("KERNEL32.DLL")` is `0xBFF70000`.
`GetProcAddress(kernel32, MAKEINTRESOURCE(n))` returns **NULL for all four
ordinals**, with `GetLastError` = `0x32` (`ERROR_NOT_SUPPORTED`). The
export-table walk (ordinal base 1, 865 functions) resolves every one:

| Ordinal | Name | Address |
|---|---|---|
| 93 | `GetpWin16Lock` | `0xBFF8E58B` |
| 96 | `_ConfirmWin16Lock` | `0xBFFB3778` |
| 97 | `_EnterSysLevel` | `0xBFF741B4` |
| 98 | `_LeaveSysLevel` | `0xBFF741ED` |

`GetpWin16Lock` returns `0x00017CE4` as the mutex's address on both guests.

**`_ConfirmWin16Lock()`**, called as a no-argument stdcall function
returning DWORD:

| State | Return |
|---|---|
| Idle, nothing taken | 0 |
| After this thread's `_EnterSysLevel(lock)` | **1** |
| After the matching `_LeaveSysLevel` | 0 |
| Inside `IDirectDrawSurface::Lock` of the **primary** (`DDLOCK_WAIT`) | **1** |
| After that `Unlock` | 0 |
| Inside `Lock` of a 64x64 **offscreen VRAM** surface with `DDLOCK_WAIT \| DDLOCK_NOSYSLOCK` | 0 |

So it returns rather than asserting when the answer is no, and DirectDraw
does hold the mutex across a primary-surface Lock and does honour
`NOSYSLOCK` on an offscreen surface, both as the `Lock` documentation says.

**Amended the same day:** the 1 is the calling thread's **recursion
depth**, not a boolean. This probe only ever entered once, so it only ever
saw 1; the HAL instrument that followed saw 2 inside every `V9xHalLock`
callback (`2026-09-26-98se-directdraw-holds-the-win16-mutex-around-every-hal-callback-measured.md`).
"Held" is non-zero.

**The structure itself** (six DWORDs at `0x00017CE4`) is `Type=4` followed
by fields that move with the lock: word 1 reads 1 while held and word 2
holds a `0xC16xxxxx` pointer (a thread database, not the `GetCurrentThreadId`
value) while held. But the idle values differ between the two guests
(`Cs1=0, Cs3=0, Cs4=1` on one; `Cs1=0xFFFFFFFF, Cs3=0xC1553A20, Cs4=0` on the
other), and one reading after `Unlock` caught `Cs0=0x8004, Cs1=1` with
`_ConfirmWin16Lock` still saying 0 - another thread mid-acquire. The
structure is live, shared and not a stable instrument; the function is.

## What this settles

- **The HAL resolves the four ordinals by walking KERNEL32's export table,
  as the plan says, and not by `GetProcAddress`**, which 98SE refuses with
  `ERROR_NOT_SUPPORTED`. The walk from a process's own mapping works; the HAL
  runs in the caller's process, so the same code serves it. The
  "inferred" note in the research record about Win95 refusing ordinals is
  now measured for 98SE as well.
- **The Phase 0.2 trace calls `_ConfirmWin16Lock()` in the callbacks and
  counts 1 against 0.** It is safe to call when the answer is 0, and 1 means
  precisely "this thread holds it", which is the question.
- The ICD's own take/release through 97/98 has been exercised from an
  application on both guests without incident, for a short hold with no
  USER, GDI or DirectDraw call inside it.

## What this does not establish

- Anything about original Windows 98, ME or 95, whose KERNEL32 may export
  or behave differently.
- That DirectDraw holds the mutex around **HAL callbacks** (Blt, Flip,
  CreateSurface, the Direct3D draws): the primary Lock result is the
  runtime's own hold around an application call, not around a driver
  callback. That is the second half of Phase 0.2, now unblocked.
- Hold times or contention; the probe holds the lock for a handful of
  instructions.

## Standing

The HAL trace for the callbacks is the next build: resolve #96 through the
walk at DriverInit, count `_ConfirmWin16Lock()` returning 1 and 0 at the
Blt, Lock, CreateSurface and DrawPrimitives entries, and read the counts
back through V9XTRACE.

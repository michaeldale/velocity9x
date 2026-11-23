# HD 4600: a Lock of the back buffer right after a vsync flip does not wait for the flip, so CPU writes land in the page still on screen

Date: 2026-11-21. Status: fixed and closed 2026-11-23, measured on the HD 4600
and the Oland 6610 (below). Earlier status: fixed in source and measured on the
HD 4600 (below); not yet run on another family. Found by dispbench's new
DB3D check `FlipSafety`, not by a program.

## What was measured

dispbench `FlipSafety` (DB3D, DirectX 7, `tools/db3d/db3d_win32.c` in the
dispbench repository, built from an uncommitted tree on `e7dd71f`). The
flip chain is double-buffered, so when a vsync `Flip` returns, the back
buffer is the page still being scanned out until the next vertical blank.
Each of 120 samples waits for a blank to end
(`WaitForVerticalBlank(DDWAITVB_BLOCKEND)`), flips (`DDFLIP_WAIT`), writes
the back buffer and times the write's completion from the blank's end. A
completion within half a refresh is a write into the page on screen.

- `Lock`: a `DDLOCK_WAIT | DDLOCK_WRITEONLY` Lock of the back buffer, one
  byte written.
- `Draw`: a colour clear and a full-screen textured quad, completed by a
  Lock of the texture (which waits for the draws that sample it, not for
  the flip).

Before sampling, the check confirms its premises: from a blank's end to the
next blank's start is most of a refresh (84-95 % here), and back-to-back
vsync flips are a refresh apart.

1024x768, boot 292, `[Velocity9x] VSync=0`:

| Run | Lock: early / judged | Lock: completion | Draw: early / judged | Draw: completion |
|---|---|---|---|---|
| 16 bpp, first | 93 / 93 | 0-5 % of a refresh | 0 / 119 | 87-154 % |
| 16 bpp, second | 97 / 97 | 0-11 % | 0 / 119 | 95-149 % |
| 32 bpp | 50 / 50 | 0-6 % | 0 / 119 | 99-215 % |

So draws and engine clears wait for the flip (the 2026-10-20 fix,
[issue](2026-10-20-haswell-3dmark2001se-flicker.md)), and a CPU Lock does
not. Evidence:
[`../probe/hd4600-flipsafety-2026-11-21/`](../probe/hd4600-flipsafety-2026-11-21/).

## The cause, from the source

`V9xHalLock` → `v9x_lock_body` (`src/display32/ddhal_core.c`) counts a
pending flip (`v9x_note_lock_flip_pending`), waits for the engine and the
render drain, and answers `DD_OK`. It never waits for the flip. The Blt
path does (`v9x_blt_flip_guard`: a destination equal to
`v9x_present_retiring` while `v9x_flip_pending()` waits in
`v9x_flip_wait_done()`, or answers `DDERR_WASSTILLDRAWING` when the caller
cannot wait), and so do the Direct3D draw paths (`d3d_hsw.c`, `d3d_g4.c`,
`d3d_i9xx.c`; `d3d_r128.c` checks the target against the retiring offset).
DirectDraw expects the driver's Lock to return `DDERR_WASSTILLDRAWING`
while a flip involving the surface is pending.

`v9x_lock_body` is shared, so the gap is likely on every family, not only
Haswell. Only the HD 4600 has been measured.

## Who it would hit

Any program that locks the back buffer to write it straight after a
`Flip`: software-rendered games and benchmarks drawing through DirectDraw,
and Direct3D programs that write the back buffer by Lock (HUDs, movie
frames). It would show as tearing or half-written frames under vsync. No
game has been seen doing it on this driver yet.

## Not explained

- In the `Lock` variant 20-60 % of Flips returned more than a quarter
  refresh after the blank's end (23 of 120, 27 of 120, 70 of 120); in
  `Draw`, 1 of 120. A guess, not checked: the flip state (`v9x_flip_done`)
  advances only when polled, so a blank that passes with nobody polling is
  missed and the next `Flip` (`DDFLIP_WAIT`) waits a further frame. Rule
  it in or out before changing it.
- The first 16 bpp run's DB3D process died after both tests had written
  their results, before its end marker. The repeat run was clean.

## The machine's VSync setting

W9QI5A83's SYSTEM.INI had `[Velocity9x] VSync=2` (every flip unsynced).
With it, back-to-back flips ran at about 3,900 a second, `FlipInterval`
answered `vsync-not-waited`, and `FlipSafety` skips (`flip-not-vsynced`):
with no blank to wait for it cannot judge. For these runs the line was
changed to `VSync=0` and the machine rebooted (boot 291 → 292); it was
left at `VSync=0`. The original file is in the probe folder as
`SYSTEM.INI-original-vsync2.txt`.

## Also found on the way

`V9XTRACE.EXE` on W9QI5A83 answers `abi-mismatch` (snapshot ABI 2026111201
against the driver's), so the driver's own counters (`DrawsIntoPresented`,
lock and flip waits) could not be read for these runs. dispbench's
`run-bench.ps1 -V9xTrace` then left the guest's `C:\V9XDIAG\V9XSNA7.INI`
overwritten with V9XTRACE's error stub; it was restored from run-bench's
backup (40,086 bytes) and run-bench now restores it whatever happens.

## Next

1. Make the Lock path follow the Blt rule: when the locked surface is the
   retiring page and a flip is pending, wait (or `WASSTILLDRAWING` under
   `DDLOCK_DONOTWAIT`), counting waits and timeouts beside
   `blt_flip_waited`.
2. Re-run dispbench `FlipSafety` on W9QI5A83 at `VSync=0`: `Lock` should
   read `held`, `Draw` stay `held`. From `C:\everything\dispbench`, in
   PowerShell:
   `scripts/run-bench.ps1 -ConfigId w9qi5a83-hd4600-v9x -Environment metal -EndpointHost 10.0.1.254 -Tools DB3D -Db3dTests FlipSafety -Db3dPerTest -Mode 1024x768x16 -Db3dDepths 16,32`
3. Run the same check on one machine of another family (GT218, Gen3) to
   confirm the gap is shared.
4. Update V9XTRACE.EXE on W9QI5A83 so the counters can be read beside the
   check.

## Fixed, 2026-11-23

`v9x_lock_flip_guard` (`ddhal_core.c`), called from `v9x_lock_body`:
a Lock of a video-memory surface whose offset is `v9x_present_retiring`,
while `v9x_flip_pending()`, waits in `v9x_flip_wait_done()`, or answers
`DDERR_WASSTILLDRAWING` under `DDLOCK_DONOTWAIT`. Waits and timeouts are
counted in `lock_flip_waited` and `lock_flip_wait_timeouts`, appended to
`V9X_D3D_DIAGNOSTICS` within ABI 2026112001 and written by V9XTRACE as
`LockFlipWaited` and `LockFlipWaitTimeouts`. `run-checks.ps1` passes.

On W9QI5A83, boot 298, the `intel-gma` set of `713bc38a-dirty` installed
by `run-release-checks.ps1` (every release probe verdict as in 0.60.0),
`VSync=0`, dispbench `381b802` (step 2's command above):

| Run | Lock: early / judged | Draw: early / judged |
|---|---|---|
| 16 bpp | 0 / 119 (`held`) | 0 / 118 (`held`) |
| 32 bpp | 0 / 119 (`held`) | 0 / 119 (`held`) |

V9XTRACE after both runs: `LockFlipPending=240`, `LockFlipWaited=240`,
`LockFlipWaitTimeouts=0`, `DrawsIntoPresented=0`, engine timeouts and
resets 0. DB3D could not read the HAL counters itself (`HalCounters=
skipped`: it reads ABI 2026102002's layout).

Not measured: whether the extra wait changes a program's frame rate, and
the Flip-return lateness in "Not explained" above. Step 3, a machine of
another family, is still owed: dispbench reaches a machine by address,
and none of the non-Intel machines online today is on the LAN.

Evidence: `docs/probe/1.0-b1-b3-b12-2026-11-23/flipsafety-hd4600/` and
`hd4600-after-flipsafety.INI`.

## Another family, 2026-11-23: the Oland

Step 3, on W9QI0DAD (Radeon Oland XT `1002:6610`, GCN), the same
command at `VSync=0`
([record](../decisions/2026-11-23-1.0-b1-oland-flipsafety.md)):

| Build | Lock: early / judged | Draw: early / judged |
|---|---|---|
| released 0.59.0, 16 bpp | 114 / 114 | 0 / 119 |
| released 0.59.0, 32 bpp | 117 / 117 | 0 / 119 |
| the fix, 16 bpp | 0 / 119 | 0 / 119 |
| the fix, 32 bpp | 0 / 119 | 0 / 119 |

`LockFlipWaited=240`, `LockFlipWaitTimeouts=0`, `DrawsIntoPresented=0`.
The gap was in the shared Lock path, as the title's "HD 4600" undersold;
measured on two families before and after. Status: fixed, closed.

Evidence: `docs/probe/1.0-b1-oland-2026-11-23/`.

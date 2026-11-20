# HD 4600: a Lock of the back buffer right after a vsync flip does not wait for the flip, so CPU writes land in the page still on screen

Date: 2026-11-21. Status: open; measured on the HD 4600 (i5-4570,
W9QI5A83), cause read in the source, not fixed. Found by dispbench's new
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

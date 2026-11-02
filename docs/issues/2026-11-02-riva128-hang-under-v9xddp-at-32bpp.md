# RIVA 128ZX: machine stops answering under V9XDDP at 800x600x32 with the NV3 engine

Date: 2026-11-02. Status: open; the machine needs its owner to power-cycle
it, and nothing has been read from it since.

Machine: M-7V636C (remote help), RIVA 128ZX 8 MiB, Windows 98 first edition.
Plan: `docs/plans/nvidia-riva128.md`, Phase 4.

## What happened

1. Boot 12 ran the first build with the 2D engine claimed (branch
   `nvidia-riva128`, uncommitted on `8a54cf41`): V9XDISP.DRV, V9XMINI.VXD and
   V9XHAL.DLL renamed into place through WININIT.INI, hashes checked equal to
   the package after the boot. `V9XHW.INI`: Acceleration
   `directdraw-fill-copy`, EngineCaps `00000003` (the hook mapped BAR0 and
   claimed fill and copy). Desktop 800x600x16, where the HAL declines the
   engine.
2. `V9XMSW /set:800x600x32`: PASS, desktop at 32 bpp, so the HAL's engine
   is in play.
3. `V9XDDP.EXE` started through the agent. The agent never answered again:
   the exec's relay call timed out, and the remote-help portal showed the
   machine offline, last seen at 09:39:08Z, within seconds of the start.

## What is known

- Nothing beyond the timing. No log, INI or screenshot came back.
- The same bring-up and the same fills and blits ran four times through the
  Phase 3 probe on this machine, at 8, 16 and 32 bpp, with no fault
  (`docs/decisions/2026-11-02-riva128-2d-engine-writes.md`). What is new in
  the HAL path: the bring-up runs inside DirectDraw's own calls, writes
  RAMIN through the framebuffer mapping rather than a VxD's, sets per-op
  clips and surfaces from DirectDraw's surfaces, and runs many operations
  in a row; and V9XDDP does far more than fill and copy (flips, locks,
  Direct3D, colour keys).
- Not ruled out: that V9XDDP stops this machine without the engine (it had
  not run on it before), or Windows 98 first edition.

## Next, once the machine is back

1. Read `C:\V9XDIAG\V9XDD.INI` (and `V9XDD2.INI`), which V9XDDP writes as it
   goes: the last key says which test it was in. Read any HAL fault trace
   (`V9XTRACE`) and `V9XBOOT.INI`.
2. Check the desktop depth it booted at (V9XMSW's switch is live; Windows
   writes the mode to the registry at shutdown, which a hang skips).
3. A control: the tier-0 HAL is still at `C:\V9XNVPKG\V9XHAL.DLL` on the
   guest. V9XDDP at 32 bpp on that HAL says whether the engine is involved.

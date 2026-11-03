# RIVA 128ZX: machine stops answering under V9XDDP at 800x600x32 with the NV3 engine

Date: 2026-11-02. Status: fixed 2026-11-03, seen on the machine (boot 15);
the cause was the HAL engine refusing validation at 16 bpp, not the engine
drawing. See "2026-11-03" below.

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

## 2026-11-03: the machine back, the cause found

The owner power-cycled it; boot 13 came up at 800x600x32 with the engine
build. Evidence copies are in the session scratchpad only (not in the
tree); the keys that matter are quoted here.

1. **Boot 12's own record.** `C:\V9XDIAG\V9XDD.INI` (V9XDDP had not run on
   this machine before, so it is that run's): `DesktopBpp=32`,
   `SetModeHr=0`, `AfterModeBpp=16`, `Result=AFTER-SETMODE`. V9XDDP got
   through its 32 bpp section, set exclusive 640x480x16, and stopped
   somewhere in the next stretch, which writes nothing to the INI until it
   ends.
2. **The engine at 32 bpp alone is fine.** V9XDDF (one engine step at a
   time, each flushed first) at the 800x600x32 desktop: PASS, fill pixel
   `8012A5C3` (the top byte the engine rewrites, so the engine drew it),
   copy exact. V9XDDP `/modeonly` (640x480x16 and back): `MODEONLY`, then
   V9XDDF again: PASS, so the bring-up after a mode set wiped RAMIN works
   too.
3. **The control, no HAL swap.** V9XMSW to 800x600x16, where
   `v9x_nv3_ready` was false at every step, then full V9XDDP: the machine
   reset itself (boot 14, no owner needed this time) with the same
   `Result=AFTER-SETMODE`. So the NV3 engine never drew in the failing
   run. V9XDDP's write-through step log `C:\V9XDIAG\V9XDDT.TXT` named the
   step: last line `key BltCanHr 00000000`, so it died in the next call,
   `v9x_hardware_fill`, a full-surface `DDBLT_COLORFILL | DDBLT_WAIT` of
   the 640x480x16 back buffer.

**Cause.** `v9x_nv3_ready` required 8 or 32 bpp, so at 16 bpp
`v9x_nv3_validate` returned 0 while `v9x_engine32()` still returned the NV3
table (it selects on engine type only). The core's CPU fallback waits on
`v9x_render_drain` before every CPU fill, copy and Lock, and that returns
ABANDONED when an engine exists but does not validate. So the colour fill
answered `DDERR_WASSTILLDRAWING` every time, and DirectDraw retries a
`DDBLT_WAIT` blit without end with the Win16 lock held. Every other engine
declines an unsupported depth per operation (the Trio64's packed 24 bpp)
and still validates, which is why no other family showed this. Boot 14's
self-reset rather than a freeze is not explained; either is a loop that
never returns.

**Fix** (`eng_nv3.c`, and `v9x_nv3_depth_drawn` in `nv3_2d.c`,
host-tested). `ready()` checks the hardware only. At a depth the engine
does not draw, validate and the idle wait answer yes without touching a
register, and fill and copy decline, so the CPU path runs.

**Seen on boot 15** (DRV, VXD and HAL from one build by WININIT rename;
installed HAL hash checked against the package, `D3F6EFF9083BF17A`):

- V9XDDP from the 800x600x16 desktop: `Result=COMPLETE`, `BltFillHr=0`,
  `BltFillPixelOk=1`.
- V9XDDP from 800x600x32 with the engine live (down to 16 and back):
  `Result=COMPLETE`, then V9XDDF at 32 bpp: PASS, `8012A5C3`.
- V9XDDF at 16 bpp: PASS, fill `07E0` with no bit 15, so the CPU served it.
- `FlipPixelOk=0` in both V9XDDP runs: flips are declined while NV3
  declares no flip capability (Phase 5), so this is expected, not new.
- 8 bpp was not run.

The desktop was left at 800x600x16, as the owner had it.

# The Mystique runs the matrox family CPU-only: enable-ok, 14 modes, GDI passes

Date: 2026-10-19

Status: measured on A8U4I5 (Michael's Mystique, 051A revision 02), boot
506, build `908c925b-dirty` (the Phase 1 change uncommitted on top of
908c925b). Phase 1 of [matrox-mystique.md](../plans/matrox-mystique.md),
on the real card only; the 86Box profiles were skipped at Michael's
request.

## What changed

- `051A` binds the matrox family: `mga_backend.c` accepts it by exact id
  (`V9X_PCI_DEVICE_MYSTIQUE`), the manifest has a third chip `mystique`,
  and the backend registry row is regenerated.
- `millennium_hw16.c` has a third device, `v9x_mystique_device`, with **no
  engine hook**. So `EngineType` is NONE, no control aperture is mapped,
  and no MACCESS or other MGA register is written. DirectDraw, GDI and
  everything else draw on the CPU.
- `framebuffer_bar` is 1, the measured order of revision 02
  ([record](2026-10-19-mystique-revision-02-bar-order.md)). Nothing reads
  it while the hook is absent.

## How it was installed

Matrox's inbox driver (`Display\0017`) was live, so none of our files were
loaded. This build's V9XDISP.DRV, V9XMINI.VXD, V9XHAL.DLL, V9XSETP.DLL,
V9XGL.DLL and V9XTRACE.EXE were put straight into SYSTEM (sizes checked),
and the Mystique's devnode was pointed at the existing Velocity9x class key
`Display\0016` (left by the Millennium II's install) with `TOV9XMY.REG`.
Then a warm restart. `TOMGAMY.REG` points it back at Matrox's driver. Both
are in [the probe folder](../probe/a8u4i5-mystique-phase1-2026-10-19/).
That class key still says "Millennium II" in Device Manager; a Have Disk
install would rename it, and was not done.

## What was measured

All in [`docs/probe/a8u4i5-mystique-phase1-2026-10-19/`](../probe/a8u4i5-mystique-phase1-2026-10-19/).

- **V9XBOOT.INI:** `Stage=enable-ok`, 800x600x16, pitch 1600,
  `Aperture=m=2 bar=1 b=dd000000` (BAR1, the prefetchable framebuffer).
  VBE 2.0, OEM revision 0106h, 24 modes listed, 19 cached, all linear at
  DD000000.
- **V9XHW.INI:** `Adapter=Matrox Mystique`, `Acceleration=none`,
  `EngineCaps=00000000`, `GdiAcceleration=none`, `Direct3DMode=none`.
  `MgaCrtcExt3=81`: the BIOS's mode set turns mgamode on, as on the 2064W.
  `VbeVramBytes` and `VramMeasuredBytes` are both 4194304: the memory walk
  agrees with the BIOS on this card.
- **V9XMODES.INI:** 14 rows, the 7 baseline modes plus 1280x1024 and
  1600x1200 at 8 and 16 bpp and 640x480 to 1024x768 at 32 bpp. Reasons
  `7,...,12`: the BIOS's 555 duplicates and the 24 bpp modes are dropped,
  as on the Millennium II.
- **V9XGDI** (no switch): `Result=PASS` at 800x600x16.
- **V9XMSW `/set`** through all 14 modes, one after another, ending at
  800x600x16: every one `Result=PASS`, `ChangeResult=0`. Each report's
  starting mode is the one before it, so none is stale.
- Screenshots before and after the sweep: the desktop draws cleanly.

## What this does not establish

- The drawing engine. Nothing has written an MGA register on this chip.
  That is Phase 2: the guarded MGA2D write probe, which also settles the
  MACCESS value (memreset and jedecrst clear).
- DirectDraw (V9XDDP) and Direct3D were not run. With no engine, D3D is
  not advertised.
- The 1164SG (revision 03) and the 86Box profiles.
- The 2064W on the BAR rule. It is not fitted; the 2164W was re-run on
  2026-10-19 (addendum to
  [the BAR rule record](2026-10-18-mga-control-bar-from-the-prefetch-bit.md)).

Gates: `run-checks.ps1` passes (check-tree, host tests with 051A now
binding the matrox backend, every family package).

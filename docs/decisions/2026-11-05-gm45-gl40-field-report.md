# The GM45 (GL40, HP t5740) runs 0.35.0: issue 22's reporter's field report

Date: 2026-11-05
Machine: issue 22's reporter's HP t5740 thin client, GL40 (GMA 4500MHD,
`8086:2A42` rev 09), Atom, Windows 98 SE (4.10.2222 A), DirectX 6.1
(4.09.00.0904 in the snapshot's `[System]`), 2 GiB.
Evidence: the reporter's comments on GitHub issue 22 (a 3DMark 99
screenshot and a description), and diagnostic report `V9X-EXR8BE` on the
update server, built `0.35.0 a4221eee`: `V9XSNAP.INI`, `V9XBOOT.INI`,
`V9XHW.INI`, `INTELMM.TXT`. Nothing here was measured by us.

## Why

The GM45 went in blind from one DOS survey
([`2026-11-04-gm45-gl40-from-a-survey.md`](2026-11-04-gm45-gl40-from-a-survey.md)),
with the GTT at BAR0 + 2 MiB resting on Linux's grouping, not re-read.
That record named the reporter's first boot as the measurement.

## What the report says

- The reporter: "everything ran okay" except flicker in some 3DMark 99
  tests, which they put down to having VSync off; "Direct3D up to version
  7 works". Which titles beyond 3DMark 99 were run is not stated. No
  OpenGL or Glide result is given.
- `V9XBOOT.INI`: `Stage=enable-ok`, aperture mapped from BAR2
  (`d0000000`), desktop 800x600x16, 5:6:5 (`is555=0`). VBE controller
  reports 1023 x 64 KiB; the driver uses 16 MiB (`vram=16777216`), as the
  survey record expected. 33 BIOS modes, the same list the survey saw;
  still no 1920x1080.
- `V9XHW.INI`: `Direct3D=hardware-gen4`, `Direct3DMode=hardware`,
  `Direct3DDdi=A`, `EngineStamp=type=00000003 caps=00443614
  ring=E64EA000`. `EngineStatusEnable=map-refused` is the normal reading
  at a first Enable on this family (`intel_hw16.c`). `GdiAcceleration=none`
  as on every Intel part. `VSync=always-off`: `[Settings] VSync=2` is set
  on this machine.
- `INTELMM.TXT`: `Result=PASS`. Pipe A runs 800x600 (HTOTAL `041F031F`,
  VTOTAL `02730257`, PIPEASRC `031F0257`), plane A (`70180` = `94000000`)
  enabled at 16 bpp, stride `0640`, base 0. Plane B disabled.
- `V9XSNAP.INI`: `HalBuild=` is empty and every HAL counter is zero.
  V9XTRACE ran after the last Direct3D process had exited and the HAL's
  shared sections were gone, so the snapshot holds nothing from the 3DMark
  run. The RTC reads 2002.

## What this settles, and what it does not

- The two open guesses for the GM45 held: the GTT at BAR0 + 2 MiB and the
  G45's 3D batches on the mobile Cantiga. A wrong GTT offset or a refused
  first batch would not have drawn 3DMark 99's tests.
- The flicker is consistent with `VSync=always-off`, which this machine
  has set, but that was not tested: no run with VSync on came back.
- One report on 0.35.0. No OpenGL, Glide or DirectX 8 result, no 3DMark
  score, and no snapshot taken while a Direct3D process was alive.
- The reporter offers the V9X remote agent on the machine for more
  hardware information.

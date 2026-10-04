# The Trio3D (`5333:8904`) on the ViRGE path: first boot on A8U4I5

Date: 2026-10-04. Machine: A8U4I5, P3 1 GHz, 440BX, Windows 98 SE.
Card: `PCI\VEN_5333&DEV_8904&SUBSYS_89045333&REV_01`, fitted in place of
the Rage XL PCI. Evidence: `docs/probe/a8u4i5-trio3d-8904-2026-10-04/`.

## Decision

`8904` is bound as a second alias of the s3 family's `virge-dx` chip, on
the Trio3D/2X's terms: the ViRGE's CR53 aperture hook and
`v9x_trio3d2x_fill_engine`, so no two-pass blend and no unlit alpha
(Michael: "this card is very similar to the trio3d/2x"). Before this, no
family bound the id and Windows ran it on Standard PCI Graphics Adapter
(VGA) at 640x480x4.

The name "S3 Trio3D 86C365" is the public PCI id list's.
`scripts/parse-vga-survey.ps1` calls `8904` a Trio3D/2X (86C362) and
`8903` the Trio3D. Not settled; no register read was taken to decide it.

The VBE tier-0 manual model was pushed to `C:\V9XVBE` and its preflight
passed, but it was not installed; the alias replaced that route.

## Host side

The manifest entry went in first. `build-host.ps1` then failed two
checks, as expected: the family-matrix test found `5333:8904` refused by
the backend probe, and the hw16 test counted 8 device entries for 9
manifest ids. Adding the id to `v9x_s3_device_ids` and
`v9x_trio3d_device` to the device list made both pass. `run-checks.ps1`
passed.

## Install

`V9XSTAGE.EXE` from the s3 package: PASS. Update Driver, Have Disk
`C:\V9XS3` offered one compatible model, "Velocity9x S3 Trio3D 86C365"
(`have-disk-model-list.png`). Windows' restart prompts were declined and
the restart was done by the agent (boot 197 -> 198, warm).

## Measured, boot 198

- `V9XBOOT.INI`: `Stage=enable-ok` at 640x480x8. Aperture from CR59/CR5A
  at `0xD8000000`.
- `V9XHW.INI`: `Adapter=S3 Trio3D 86C365`, `Direct3D=hardware-s3d`,
  `VideoMemoryBytes=2097152`, core and memory clock 75,170 kHz,
  `GdiAcceleration=gdi-fill-copy-overlap-text`, `ColourLayout=555-auto`.
- `V9XMSW.INI`: live switch 640x480x8 -> 1024x768x16, `Result=PASS`.
- `V9XACCE.INI` (`V9XGDI /accel` at 1024x768x16): `Result=PASS`, 500
  operations, 20 comparisons against the DIB engine all equal, no idle,
  FIFO or reset events outside the deliberate fault injection.
- `V9XDD.INI` (V9XDDP): `Result=COMPLETE`, HAL device hardware. Pass:
  triangle fill and every one of 12 shapes, slot permutations, base and
  tiled texture, mip level select, colour key, vertex alpha, 4444 and
  1555 formats, perspective, 48 of 48 texture-matrix rungs.
  Fail: specular (Gouraud and textured), fog (vertex and depth),
  trilinear blend, bilinear (`Tex8BilinearOk`), the 2x2 texture and the
  565-reference 8x8 texture, decal alpha, clamp, `ChainB`, `ChainRows`,
  `MipTri`, alpha curves F1, F2 and F4. Z and Mixed did not run: every
  call returned `0x88760231`, as on the Rage XL on 2026-10-03.

None of the failures has been compared with the Trio3D/2X's matrix on
this machine, so which are this chip and which are the probe at 2 MiB is
not known.

## Disputed or unresolved

- **Memory size.** CR36 bits 7:5 = 4, which the ViRGE/Trio64 decoder
  reads as 2 MiB. VBE `4F00h` reports 127 64 KiB blocks (about 8 MiB).
  The driver used CR36, leaving a 512 KiB DirectDraw heap at
  1024x768x16 (`GblHalVidMemTotal=0x80000`). The Trio3D/2X note in
  `memory.c` says its encoding differs from the ViRGE's; whether the
  Trio3D's does is not measured. If the card has more than 2 MiB, the
  driver is under-using it, not overrunning it. A write-read walk past
  2 MiB in the aperture would settle it.
- **LFB address.** Every VBE mode reports its linear base at
  `0xDC000000`, 64 MiB above the `0xD8000000` the driver maps. The
  desktop, GDI read-back and Direct3D read-back all go through the
  driver's mapping and agree with each other. Nobody has looked at the
  monitor, so whether the scanout matches has not been checked.
- The Trio3D/2X's two-state blend trap was not looked for on this card.

# A8U4I5 with an ATI 3D Rage XL PCI: first boot on Velocity9x

Date: 2026-10-03. Card `PCI\VEN_1002&DEV_4752&SUBSYS_80081002&REV_27`,
8 MB, swapped in for the Rage IIC AGP. HAL from tree `e3bf6d3-dirty`,
which is `eca481d`'s source. Package staged in `C:\V9XPKG` from HEAD
`276a07b`.

## Install

Windows bound ATI's own driver to the new card at first detection
("RAGE XL PCI? (4752/HPTC)", ATI Tech. - Enhanced, 640x480x4). The
Velocity9x INF it already had (`INF\OTHER`, 3,765 bytes) predates the
Rage XL alias.

Velocity9x went on through Update Driver, then Have Disk `C:\V9XPKG`,
which offered one model, "Velocity9x ATI Rage XL PCI". Windows warned
that the current driver "may be a closer match": ATI's INF names the
subsystem, and ours matches vendor and device only. Then a warm restart
(boot 181). `V9XSTAGE.EXE` run under ATI's driver beforehand exited
`0xFFFFFFFD` with no report; this was not investigated.

## first-boot/

- `V9XHW.INI`: `Adapter=ATI 3D Rage XL PCI`, `Direct3D=hardware-mach64`,
  `Acceleration=directdraw-fill`, 8,323,072 bytes of VRAM.
- `V9XBOOT.INI`: `Stage=enable-ok` at 640x480x8, the default first mode.
- `V9XMSW.INI`: live switch to 1024x768x16, `Result=PASS`.
- `V9XSYNC.INI`: `Reason=multiple-marked-instances`. The Rage IIC's
  `Display` key and the XL's both carry `V9xFamily=ati`.
- `V9XDD.INI` (V9XDDP): `Result=COMPLETE`. Every triangle, shape, fog,
  specular, texture-format, colour-key and mip check passes. The Z and
  Mixed sub-tests did not run (`hr=0x88760231` at surface creation, as on
  the Gateway's Rage Mobility on 2026-09-28).
- `V9XSNA7.INI`: engine type 4 (Mach64), 528 draws / 531 triangles,
  38 refused (36 `ALPHA_FORCE`, the probe's own; one colour key; one
  texture format), and `FlipDeclined=23`.

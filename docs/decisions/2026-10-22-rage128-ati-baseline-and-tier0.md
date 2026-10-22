# Rage 128 Pro on A8U4I5: ATI's driver as the reference, then tier-0

Date: 2026-10-22

Status: **measured, one machine.** A8U4I5 (Pentium III 1000 MHz, 440BX,
256 MiB, DirectX 9.0c), card swapped in by Michael for 0.26. Boot 518 ran
ATI's driver; boot 519 ran Velocity9x tier-0 from `6aaa252f-dirty`
(the change this record commits). Evidence:
[`../probe/a8u4i5-rage128-ati-baseline-2026-10-22/`](../probe/a8u4i5-rage128-ati-baseline-2026-10-22/)
and [`../probe/a8u4i5-rage128-tier0-2026-10-22/`](../probe/a8u4i5-rage128-tier0-2026-10-22/).
Plan: [ati-rage128.md](../plans/ati-rage128.md).

## The card

Read from the registry at boot 517, before any driver was installed:

| | |
|---|---|
| PCI id | `1002:5046`, "PF", a Rage 128 Pro, revision 00 |
| Subsystem | `0018:1002`, which ATI's INF names "Rage Fury Pro/Xpert 2000 Pro" |
| Bus | AGP (`Enum\PCI\...\000800`) |
| Resources | memory `DC000000`-`DFFFFFFF` (64 MiB) and `E1000000`-`E1003FFF` (16 KiB), plus I/O |
| Memory | 32 MiB: VBE 4F00h says 512 x 64 KiB, and ATI's HAL reports 32,660 KB |
| BIOS | VBE 2.0, OEM revision 0100, 56 modes listed, 50 cached with a linear framebuffer |

Windows 98 SE has no driver for it and had bound Standard PCI VGA
(`Display\0018`, vga.drv, 640x480 at 4 bpp).

## ATI's driver

ATI's last Windows 9x release for the Rage 128 family:
`wme_r128_4_13_7192.exe`, 12,562,724 bytes, SHA-256
`21ba14dcd5f12ee2d1e5892269899474cc0a5e515a19a09f304ae6527266cf4c`,
from the archive.org collection `win9x_rage128_drivers`. INF `DriverVer`
09/22/2001, 4.13.7192; the class key reads `ReleaseVersion`
6.60-010926m-1476C-ATI. It carries a Direct3D HAL (`ATI3DRAA.DLL`, which
3DMark reports as 4.13.01.8139) and an OpenGL ICD (`ATIO9XAA.DLL`).
The installer is kept outside the repository, in
`build\external\rage128\driver-4.13.7192\`.

It went in unattended: unpacked on the guest with 7-Zip and run as
`SETUP.EXE -s` against its own `setup.iss` (log `ResultCode=0`). The
devnode stayed `Display\0018`, now ATI's. After the restart, the package's
"Specialized MVD WDM VBI Codec" asked for `ati9xwdm.cat` on the Windows CD;
that file is in ATI's own `ATIDRIVE` folder, and pointing the dialog
there finished the install. The desktop was set to 800x600x16 live with
V9XMSW.

### 3DMark 2000 default run

3DMark 2000 v1.1 (build 340, unregistered), 1024x768, 16-bit colour, 16-bit
Z, 16-bit textures, triple buffering, on an 800x600x16 desktop, started
through the agent.

| Test | Result |
|---|---|
| 3DMark result | **2404** |
| CPU speed | 255 CPU 3D marks |
| Game 1 Helicopter, low / medium / high | 42.6 / 32.8 / 16.1 fps |
| Game 2 Adventure, low / medium / high | 41.0 / 37.9 / 29.9 fps |
| Fill rate, single / multi texturing | 128.6 / 178.2 Mtexels/s |
| High polygon count, 1 / 4 / 8 lights | 2341 / 2067 / 1864 KTriangles/s |
| Texture rendering, 8 / 16 / 32 MB | 136.4 / 134.1 / 121.2 fps |
| Texture rendering, 64 MB | "Not enough AGP memory" |
| Bump mapping, emboss 3 / 2 / 1-pass | 75.9 / 86.8 / 130.5 fps |
| Bump mapping, environment | Not supported |

The HAL reports 3 texture blending stages, 2 textures in one pass, 16 and
32-bit rendering (not 24), table, vertex and range fog, and textures up to
1024x1024.

### Half-Life mwd5

Half-Life 1.1.1.0, 640x480x16 fullscreen, `HL.EXE -console -condebug
+timedemo mwd5`. Michael clicked Console on the menu and typed the two warm
runs. Scores from `valve\qconsole.log`, read after each session; the log
before held only the Mystique's lines.

| Renderer | Run 1, cold | Run 2 | Run 3 |
|---|---|---|---|
| Direct3D (EngineType 3) | 34.664 fps (394 frames, 11.366 s) | 59.233 fps | 59.470 fps |
| OpenGL (EngineType 2) | 61.105 fps (394 frames, 6.448 s) | 70.261 fps | 70.082 fps |

The warm Direct3D runs sit just under 60. Whether ATI's HAL waits for the
vertical blank in this timedemo was not checked; the figure may be a
refresh cap rather than the card's speed. The OpenGL renderer string was
not captured. `OpenGLDrivers` lists `atio9xaa` beside six older ICDs, and
Windows picks the one the display driver names.

These are reference figures for the same machine, not targets
(`no-3dmark-performance-chasing`).

## Velocity9x tier-0

The `ati` family gains a fourth chip, `rage128` (`5046` only), with no hw16
hook: `rage128_hw16.c`. The Rage 128 is not a Mach64. Its register window
is 16 KiB with no `CONFIG_CHIP_ID` at the Mach64's offset, so the family's
engine code and the mini-VDD's ATI register map must never see it. With no
hook, nothing reads a BAR; the VBE sets modes and the CPU draws.

Installed by WININIT rename of the eight runtime files (renames only)
over the existing Velocity9x ATI class key `Display\0007`, and a `.reg`
pointing the devnode there at 800x600x16 (`TOV9X128.REG`; `TOATI128.REG`
puts ATI's `Display\0018` back). Boot 519:

- `Stage=enable-ok` at 800x600x16, aperture BAR0 `DC000000`, pitch 1600.
- V9XHW: `Adapter=ATI Rage 128 Pro`, `DeviceId=5046`,
  `VbeVramBytes=33554432`, `Acceleration=none`, `Direct3DMode=none`.
- V9XMODES: 30 modes published (7 baseline, 23 dynamic), up to
  1280x1024 at 8, 16 and 32 bpp, of the 50 the BIOS listed with a linear
  framebuffer. The 15- and 24-bpp modes are not among them.
  `Vram=reported=33554432 usable=16777216`: the family maps 16 MiB, so
  half the card is unused at tier-0.
- V9XGDI `/auto` PASS.
- V9XMSW `/set` PASS in 7 modes, live, each starting from the one before:
  640x480x8, 1024x768x16, 1280x1024x16, 1024x768x32, 1280x1024x32,
  800x600x8, 800x600x16.
- V9XDDP `Result=COMPLETE`, no key reads FAIL. Its Direct3D keys are the
  CPU engine's.

Gates: `run-checks.ps1` passed on the tree committed with this record.

## Not established

- Whether ATI's warm Direct3D figure is vsync-capped.
- Anything about the Rage 128's registers. Nothing has been read yet.
- The other Rage 128 ids. Only `5046` is bound; the research lists 37.

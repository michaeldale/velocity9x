# Rage IIC on ATI's own driver at 640x480: 3DMark 99 484, Final Reality 3.81, no OpenGL

A second set on the community 4.11.2611 build follows; it lands within a
few percent of these, and refuses OpenGL the same way.

Date: 2026-10-02
Machine: A8U4I5, 10.0.1.172, Pentium III 1002 MHz (CPUID 6/8/6), 256 MiB,
Intel 440BX, Windows 98 SE (4.10.2222 A), DirectX 4.09.00.0904, boot 124.
Card: ATI 3D Rage IIC AGP, `PCI\VEN_1002&DEV_4757&SUBSYS_47571002&REV_7A`,
4 MiB (3DMark reports 4074 KB).
Driver: ATI `w82474en.exe` 4.11.2474 (MACXW4, INF date 12-10-1998), SHA-256
`75ba8660...43b8b8`, from archive.org `atirageiicwin98driver`. Installed with
ATI's own setup over Microsoft's Standard PCI VGA; desktop 1024x768x16.
Evidence: [`../probe/a8u4i5-rage-iic-native-2026-10-02/`](../probe/a8u4i5-rage-iic-native-2026-10-02/)

A native-driver baseline, recorded at Michael's request before any
Velocity9x work on this chip. Per the 2026-09-26 rule the figures are
recorded, not chased.

## Why this driver

4.11.2474 is the last ATI release AMD's legacy page lists for Rage IIC.
The 4.13 `ATI2DRAE` line is Rage Pro and later. A VOGONS-built 4.11.2611
package lists 4757 but is modified, not ATI-released; it is the second
run, not this one. Desk research only; no other ATI release was tried.

## Results

| Test | Settings | Result |
|---|---|---|
| 3DMark 99 Max | 640x480 16-bit, 16-bit Z, triple buffer, P3 optimisations, all tests | **484** 3DMarks, 13614 CPU |
| Final Reality 1.01 | standard run, Direct3D on-board, no sound | **3.81** Reality Marks (2D 6.21, 3D 1.68, bus 6.84) |
| Quake 2 demo, OpenGL | `+set vid_ref gl +set gl_mode 3` | **does not start**: `GLimp_InitGL failed`, falls back to ref_soft |
| Quake 2 demo, software | `+set vid_ref soft +set sw_mode 3 +set timedemo 1`, attract loop | **46.0** fps (45.8-46.3 over 8 passes) |
| Half-Life 1.1.1.0 mwd5, Direct3D | `-d3d -w 640 -h 480 -full`, 16 bpp | 6.128, 6.599, **6.627** fps |
| Half-Life 1.1.1.0 mwd5, OpenGL | `-gl` | **refused**: "The selected OpenGL mode is not supported by your video card" |
| Half-Life 1.1.1.0 mwd5, software | `-soft` | 32.385, 33.264, **33.264** fps |

3DMark 99 per test (Result Browser, Details): rasterizer 94 3DRasterMarks;
Game 1 5.3 fps, Game 2 4.5 fps; fill rate 7.7 MTexels/s, the same with
multi-texturing; texture rendering 2/4/8/16/32 MB 26.0/18.8/11.4/6.7/3.6
fps; bump mapping not supported; point sample 179.5 %, bilinear 100 %,
trilinear 100.4 %, anisotropic not supported; polygons 6/25/50/250/1000
pixel individual 388.4/160.0/91.0/22.5/6.2 KPolygons/s, strips
380.1/156.1/87.3/21.0/6.0; refresh 73 Hz; no missing features listed.
3DMark's own display dialog offered 640x480, 720x480, 800x600 and
848x480 at 16 bit, with 2538 KB texture memory available.

Final Reality 3D tests: 25 pixel 131.14 Kpolys/s, Robots 7.94 images/s,
fill rate 6.79 Mpixels/s, City 9.66 images/s, visual appearance 88.89 %.
Bus: 2D 143.03 MB/s, 3D 142.47 MB/s.

## What the evidence says about OpenGL

The INF registers `OpenGLDrivers\ATOGLRP9 = atoglrp9` for every chip it
binds, the Rage IIC included, and copies `ATOGLRP9.DL_` (a Rage Pro
ICD). Both GL games refuse it on this card: Quake 2's ref_gl fails
`GLimp_InitGL` at mode 3 and at its safe-mode retry, Half-Life rejects
the mode. The desk research said ATI shipped no Win9x ICD for Rage II/IIC;
the INF says otherwise, and the measurement says the registered ICD does
not work here. Why it fails (pixel format, chip check, mode) was not
looked into. Neither game ran any GL frames, so there is no native OpenGL
baseline on this card.

## Second set: the community 4.11.2611 build

`5.40x2611x3D.7z` from VOGONS (file id 227500), SHA-256
`c19667cb...e5fe965`: ATI files repackaged by a forum member with an INF
(`ATIi9xad.inf`) that lists 4757; not an ATI release. Installed over
4.11.2474 with Update Driver / Have Disk from `C:\ATI2611`, model
"RAGE IIC AGP (4757)". Display Properties then reads chip Mach64GT,
software version 5.40x2611x3D, `ati2ddad.drv` / `ati2ddad.vxd`; 4.11.2474
read 5.30-C9W-WEB, `macxw4.drv`. The install left a pending WININIT.INI
holding one rename and no `NUL=` line; boot 125. The same procedures and
settings as above, the same day; 2474's display class is saved as
`display-class-after-2474.reg` for a rollback.

| Test | 4.11.2474 | 4.11.2611 community |
|---|---|---|
| 3DMark 99 Max | 484 / 13614 CPU | **490** / 13601 CPU |
| Final Reality | 3.81 (2D 6.21, 3D 1.68, bus 6.84) | **3.82** (2D 6.20, 3D 1.68, bus 6.88) |
| Quake 2 OpenGL | `GLimp_InitGL failed` | `GLimp_InitGL failed` |
| Quake 2 software | 46.0 fps | **46.0** fps |
| Half-Life D3D, best of 3 | 6.128, 6.599, 6.627 | 6.184, 6.598, **6.629** |
| Half-Life OpenGL | refused | refused |
| Half-Life software, best of 3 | 32.385, 33.264, 33.264 | 32.458, 33.491, **33.491** |

3DMark 99's per-test figures agree with 4.11.2474's to 0.1 fps or
KPolygons/s everywhere (2 MB textures 25.9 against 26.0, 16 MB 6.6
against 6.7, 32 MB 3.5 against 3.6; refresh 74 Hz against 73). One run of
each, so the differences are inside what a repeat would show and are not
attributed to the driver. The community build ships its own
`Atoglrp9.dl_`; it fails both GL games exactly as ATI's does.

## Not established

- The pictures. Nobody watched a run, and Final Reality's 88.89 % visual
  appearance is its own score, not an inspection.
- Whether the Half-Life D3D figure renders the frame completely.
- Run-to-run spread for 3DMark and Final Reality: one run each.
- Sound: none of the four had a working sound device (Half-Life warned at
  install, Quake 2 `dsound init failed`).

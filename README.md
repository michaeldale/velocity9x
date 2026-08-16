# Velocity9x

A ground-up Windows 9x display driver for S3 graphics chips, written from
scratch against the Windows 98 DDI, DIB Engine, DirectDraw HAL and Direct3D
HAL contracts.

**Version 0.3** — see [CHANGELOG.md](CHANGELOG.md).

> **This is an engineering bring-up driver, not a release driver.** It has been
> developed and tested almost entirely under [86Box](https://86box.net/).
> Install it only on a virtual machine you have backed up cold, or on hardware
> you are willing to recover by hand. Read [docs/INSTALL.md](docs/INSTALL.md)
> before you install anything.

![The Velocity9x page in Windows 98 Display Properties, showing an S3 ViRGE/DX
at 800x600x16 with the linear aperture mapped and a passing GDI test](docs/images/velocity9x-display-properties.png)

## What it does

Velocity9x replaces the Windows 98 display driver for a supported S3 card. It
provides:

- **Display modes** — 640x480, 800x600 and 1024x768 at 256 colours and High
  Color (16-bit), plus 640x400 at 256 colours. Resolution *and* colour-depth
  changes apply live on the ViRGE, without a reboot.
- **2D output** — through the system DIB Engine, with the framebuffer mapped
  linearly. GDI drawing itself is not hardware-accelerated.
- **DirectDraw acceleration** — a flat 32-bit HAL (`V9XHAL.DLL`) providing
  video-memory surfaces, CRTC display-start page flipping, genuine
  vertical-blank services, hardware solid colour fills and hardware
  screen-to-screen BitBLT.
- **Direct3D acceleration** (ViRGE only) — a deliberately narrow but real
  hardware path through the S3D engine: textured, Gouraud-shaded,
  perspective-correct triangles with mipmapping, trilinear filtering, alpha
  blending, specular highlights, fog and Z testing.
- **A Velocity9x page inside Display Properties** reporting the detected
  adapter, PCI ID, installed video memory, active mode and clock, which
  acceleration paths are live, and the driver's own runtime diagnostics.

## Supported chips

| | **S3 ViRGE/DX 86C375** | **S3 Trio32/64 86C764** |
|---|---|---|
| PCI ID | `5333:8A01` | `5333:8811` |
| Package | `build/win98se-s3` (one binary serves both) | `build/win98se-s3` |
| Status | Primary target | Conservative baseline |
| Display modes | 640x400x8, 640/800/1024 at 8 and 16 bpp | same |
| Live resolution change | Yes | Yes |
| Live colour-depth change | Yes | Yes |
| DirectDraw surfaces / page flip / vblank | Yes | Yes |
| Hardware colour fill | Yes (S3D) | Yes (8514/A) |
| Hardware BitBLT | Yes (S3D) | Yes (8514/A) |
| Direct3D | Yes (narrow S3D path) | **No** — deliberately not advertised |
| GDI acceleration | No | No |
| Hardware cursor | No (software cursor) | No (software cursor) |

The Trio32/64 target is intentionally a software-GDI plus DirectDraw baseline.
The ViRGE-only new-MMIO window, the S3D engine and Direct3D are not exposed on
it. Its bring-up and boundaries are recorded in
[docs/decisions/2026-08-14-trio64-bringup.md](docs/decisions/2026-08-14-trio64-bringup.md).

A third target, the **Matrox Millennium II**, exists only in the Win16 skeleton
and loader-probe build scripts. It is exploratory, is not produced by the
active package builder, and should not be treated as supported.

## Have an unsupported card?

Writing a backend for a chip means knowing what is actually on the board, and
a photograph of the silkscreen is not enough. `V9XSURV.EXE` collects it: the
PCI identifiers and full configuration space, the video BIOS, the VBE mode
list, your monitor's EDID and the raw VGA register file.

**Download it from the [`survey-v1` release](https://github.com/michaeldale/velocity9x/releases/tag/survey-v1).**
It is a real-mode DOS program — that is the only place one executable can read
all of the above without a driver. Boot to DOS (`Start` → `Shut Down` →
*Restart in MS-DOS mode*), run `V9XSURV`, and send back the
`C:\V9XSURV.INI` it writes. A DOS box inside Windows also works; it just sees
less.

It reads, it does not write: no mode change, nothing installed, nothing left
behind on the card. The one step that writes anything is the opt-in vendor
probe, which sets the documented unlock keys for your chipset family, reads
the registers behind them and restores the originals — it is asked as a
question, the main report is already on disk before it runs, and declining
costs you only that section.

The report is plain text — open it before you send it. It holds hardware
identifiers and register values only, with one exception worth knowing about:
your monitor's EDID carries its model and factory serial number.

What the tool captures and why, including the safety tiers and the per-vendor
probe support, is specified in
[docs/specifications/vga-survey.md](docs/specifications/vga-survey.md).
Reports are decoded host-side by
[scripts/parse-vga-survey.ps1](scripts/parse-vga-survey.ps1), so a decoding
mistake is fixed by editing a script and re-running it over every report
already collected, rather than by shipping a new executable to everyone who
helped.

## The Direct3D path, as a third party sees it

![Final Reality 1.01 Advanced Options running on Velocity9x, listing the
Direct3D capabilities it detected](docs/images/final-reality-d3d-capabilities.png)

Final Reality 1.01 selects "Direct3D On-board Accelerator" and enumerates the
driver's Direct3D device. The capabilities it lights up — bi-linear filtering,
Z-buffer sorting, mip-mapping, tri-linear mapping, depth fog, specular Gouraud,
vertex alpha, crossfade alpha blending and subpixel accuracy — are the ones the
S3D path actually implements. Additive and multiplicative alpha stay greyed
out, which is correct: the driver declares only `SRCALPHA`/`INVSRCALPHA`
blending, which is the crossfade case and nothing more.

This is a useful sanity check on the capability table, because it is an
independent reading of what the driver advertises rather than the driver
describing itself.

## How it compares to the retail S3 drivers

Honest summary: for 2D desktop use Velocity9x is close to the retail driver on
both chips; for 3D it is far behind, and on the Trio64 there is no 3D at all.

**Where it matches the retail driver**

- The full 8/16-bpp mode matrix, live resolution switching, and — on the ViRGE
  — live colour-depth switching, which retail Windows 9x drivers of this era
  generally do *not* do.
- DirectDraw fundamentals: video-memory surfaces, real page flipping off the
  CRTC display-start register, true vertical-blank waits, accelerated fills
  and screen-to-screen copies. A `BltFast` presentation path measured 18 FPS
  (ViRGE) and 16 FPS (Trio64), level with a direct-backbuffer path.

**Where it is behind the retail driver**

- **No GDI acceleration.** The retail drivers accelerate desktop blits, fills
  and line drawing through the same 2D engine Velocity9x only uses for
  DirectDraw. Desktop drawing here goes through the DIB Engine in software.
- **No hardware cursor.** The retail drivers use the chip's cursor; Velocity9x
  draws a software cursor.
- **Direct3D is a subset.** Against the retail S3 ViRGE driver's Direct3D
  device description, Velocity9x declares `dwTextureCaps` `0x27` versus
  `0x2F`; the difference is colour-key transparency. It also lacks
  `SORTINCREASINGZ` and `SPECULARFLATRGB`, accepts only pre-transformed and
  pre-lit vertices, and does no clipping, backface culling, lines or indexed
  primitives. The S3D triangle engine writes native ZRGB1555 into a surface
  described as RGB565, which is an unresolved mismatch.
- **Fewer modes.** No 24/32-bpp modes, and no resolutions above 1024x768.
- **DirectDraw low-resolution modes are unreliable.** 640x400 is reachable
  from GDI but not from `SetDisplayMode`, and the 320x200/320x240 ModeX path
  reports success then fails in use. Applications configured for those modes
  can crash. See
  [docs/issues/2026-08-15-doom95-low-resolution-modes.md](docs/issues/2026-08-15-doom95-low-resolution-modes.md).

Real-application results, including where the driver is known to fall short,
are recorded under [docs/issues](docs/issues).

## Getting started

| Task | Read |
|---|---|
| Install on a test VM or machine | [docs/INSTALL.md](docs/INSTALL.md) |
| Get it onto an offline machine | [Transfer disk](#transfer-disk) below |
| Recover a machine that will not boot | [packaging/win98se/RECOVER.TXT](packaging/win98se/RECOVER.TXT) |
| Build from source | [docs/BUILDING.md](docs/BUILDING.md) |
| Understand the design | [docs/specifications/win9x-driver-boundaries.md](docs/specifications/win9x-driver-boundaries.md) |
| See what changed | [CHANGELOG.md](CHANGELOG.md) |

## Transfer disk

For a machine with no network, build a folder that fits one 1.44 MB floppy:

```powershell
./scripts/build-floppy-package.ps1
```

The result is `build/floppy`, about 460 KB, carrying **both** chip packages
so the target machine can pick the one that matches its card:

```
README.TXT     what this is, which folder to use, install and recovery
RECOVER.TXT    recovery steps, at the root so they are findable in a hurry
VIRGE\         S3 ViRGE/DX 86C375   (PCI 5333:8A01)
TRIO64\        S3 Trio32/64 86C764  (PCI 5333:8811)
```

Copy the whole tree to a formatted floppy, or to any other medium the machine
can read. Nothing is archived, because Windows 98 has no built-in extractor
and an offline machine may have no unzip tool — the files are usable straight
off the disk. Add `-Zip` if you want an archive for network transfer instead.

Each package carries a `SHA256.TXT`; after copying you can confirm nothing was
corrupted in transit.

## Reporting problems

Include the chip and PCI ID, the package build identifier, the display mode in
use, and the contents of `C:\V9XBOOT.INI`. If DirectDraw or Direct3D is
involved, add `C:\V9XDD.INI` and `C:\V9XTRACE.INI`. If the machine failed to
reach the desktop, a COM1 serial capture is the most useful single artefact —
[docs/INSTALL.md](docs/INSTALL.md) explains how to set one up.

## Safety and licensing

Always keep a standard-VGA fallback and a cold backup. Never install directly
from `packaging/win98se`; build the package and read the `FIRSTBOOT.TXT`,
`INSTALL.TXT` and `RECOVER.TXT` it contains.

Copyright (c) 2026 Michael Dale.

Velocity9x is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

It is distributed in the hope that it will be useful, but **without any
warranty** — without even the implied warranty of merchantability or fitness
for a particular purpose. See the [GNU General Public License](LICENSE) for
details.

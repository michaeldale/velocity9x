# Velocity9x

A ground-up Windows 9x display driver, written from scratch against the Windows
98 DDI, DIB Engine, DirectDraw HAL and Direct3D HAL contracts. It began as an S3
driver and now has native support for S3 and ATI chips plus a generic VESA path
that runs on cards it has never been told about.

**Version 0.4.2** — see [CHANGELOG.md](CHANGELOG.md).

> **This is an engineering bring-up driver, not a release driver.** Most of its
> development and testing happens under [86Box](https://86box.net/); as of
> 0.4.2 the S3 Trio32/64 target is also verified on a physical card, and every
> other target remains emulator-only. Install it only on a virtual machine you
> have backed up cold, or on hardware you are willing to recover by hand. Read
> [docs/INSTALL.md](docs/INSTALL.md) before you install anything.

![The Velocity9x page in Windows 98 Display Properties, showing an S3 ViRGE/DX
at 800x600x16 with the linear aperture mapped and a passing GDI test](docs/images/velocity9x-display-properties.png)

## What it does

Velocity9x replaces the Windows 98 display driver for a supported card. How much
you get depends on the card: an S3 ViRGE gets the full stack down to Direct3D,
while an unlisted VESA card gets a working unaccelerated desktop. What every
target gets:

- **Display modes** — 640x480, 800x600 and 1024x768 at 256 colours and High
  Color (16-bit), plus 640x400 at 256 colours. Resolution *and* colour-depth
  changes apply live on the ViRGE, without a reboot.
- **2D output** — through the system DIB Engine, with the framebuffer mapped
  linearly. GDI drawing itself is not hardware-accelerated.
- **DirectDraw** — a flat 32-bit HAL (`V9XHAL.DLL`) providing video-memory
  surfaces, CRTC display-start page flipping and genuine vertical-blank
  services on every target. Solid colour fills and screen-to-screen BitBLT run
  on the chip's 2D engine where there is a backend for one (both S3 parts) and
  fall back to the CPU where there is not (ATI, generic VESA).
- **Direct3D acceleration** (S3 ViRGE only) — a deliberately narrow but real
  hardware path through the S3D engine: textured, Gouraud-shaded,
  perspective-correct triangles with mipmapping, trilinear filtering, alpha
  blending, specular highlights, fog and Z testing.
- **A Velocity9x page inside Display Properties** reporting the detected
  adapter, PCI ID, installed video memory, active mode and clock, which
  acceleration paths are live, and the driver's own runtime diagnostics.

## Supported chips

Cards are grouped into *families*, one built package each. A family's driver
binary serves every chip in it and picks the right one by PCI id at boot.

| | **S3 ViRGE/DX** | **S3 Trio32/64** | **ATI Mach64 / Rage** | **Generic VESA** |
|---|---|---|---|---|
| PCI ID | `5333:8A01` | `5333:8811` | `1002:5654`, `1002:4C4D` | `1234:1111`, or anything via Have-Disk |
| Package | `build/win98se-s3` | `build/win98se-s3` | `build/win98se-ati` | `build/win98se-vbe` |
| Status | Primary target | Conservative baseline | Tier-0 bring-up | Tier-0 fallback |
| Display modes | 640x400x8, 640/800/1024 at 8 and 16 bpp | same | same | same |
| Live resolution change | Yes | Yes | Yes | Yes |
| Live colour-depth change | Yes | Yes | Yes | Yes |
| DirectDraw surfaces / page flip / vblank | Yes | Yes | Yes | Yes |
| Hardware colour fill | Yes (S3D) | Yes (8514/A) | **No** — CPU | **No** — CPU |
| Hardware BitBLT | Yes (S3D) | Yes (8514/A) | **No** — CPU | **No** — CPU |
| Direct3D | Yes (narrow S3D path) | **No** | **No** | **No** |
| GDI acceleration | No | No | No | No |
| Hardware cursor | No (software cursor) | No | No | No |

The Trio32/64 target is intentionally a software-GDI plus DirectDraw baseline.
The ViRGE-only new-MMIO window, the S3D engine and Direct3D are not exposed on
it. Its bring-up and boundaries are recorded in
[docs/decisions/2026-08-14-trio64-bringup.md](docs/decisions/2026-08-14-trio64-bringup.md).

### Verified on physical hardware

0.4.2 is the first release proven on a real card rather than an emulator. The
full stack — the driver, its DirectDraw HAL and its own mini-VDD — runs on a
physical **S3 Trio64 (86C764, 2 MB, Windows 98 SE)**: desktop at 1024x768x16,
video memory sized from the chip, hardware fills and screen-to-screen blits on
the 8514/A engine, CRTC page flipping and real vertical-blank services, and the
Display Properties page reporting the card correctly.

It is also *faster than S3's own Windows 98 driver* on that card. In Ironfield
RTS at 640x480 fullscreen, against the stock driver on the same machine:

| Presentation path | Velocity9x | Stock S3 |
|---|---|---|
| Direct back buffer | **27 FPS** | 25 FPS |
| Video memory + `BltFast` | **27 FPS** | 23 FPS |
| System RAM | 20 FPS | 22 FPS |
| Windowed | 18 FPS | 18 FPS |

Getting there took two bugs that only real silicon exposed: a 4 MiB video-memory
assumption that is wrong on a 2 MB card, and a mini-VDD that allocated a V86
scratch buffer without paragraph alignment and then truncated its address to a
real-mode segment — harmless on every emulated BIOS, a boot-time protection
error on the physical one. Both are written up in
[docs/issues/](docs/issues/).

### Tier-0: how a new card starts

The two right-hand columns are **tier-0**: the mode is set through the VESA BIOS,
the framebuffer address comes from the BIOS too, and the CPU does all the
drawing. No chip register is touched, so the same code drives any card with a
VESA 2.0 BIOS and a linear framebuffer. Every new chip starts here, and a native
backend adds acceleration on top later — the ATI family is at that stage now,
awaiting a Mach64 2D engine.

The cost is measurable. On Ironfield RTS's `BltFast` presentation path, tier-0 on
a Mach64 gets 6 FPS where the ViRGE's hardware blitter gets 18, because every
byte of the frame crosses the aperture twice through the CPU. That gap is what a
native engine buys.

**Known issue:** 16 bpp modes display incorrectly on the Mach64 — 8 bpp is
correct at every resolution. Tracked as `D5` in
[docs/issues/2026-08-16-tier0-defects-deferred.md](docs/issues/2026-08-16-tier0-defects-deferred.md).

The **Matrox Millennium II** family (`102B:051B`) builds as a guarded drop-in
candidate rather than an INF package, because the machine it targets has no
recoverable install path. It has never been run on its physical card and should
not be treated as supported.

## Have an unsupported card?

**Try the `VBE\` package.** Tier-0 needs nothing but a VESA 2.0 BIOS and a linear
framebuffer, so it stands a fair chance on a card nobody here has seen. Its INF
lists one PCI id, because that is all anyone has tested — to use it on anything
else, pick it explicitly through **Have Disk** in the Display Properties adapter
dialog and accept the "not intended for this hardware" warning. That is a
deliberate design: Windows should never bind this driver automatically to a card
it was not verified on, but you should be able to choose it.

It is genuinely tested that way rather than in theory. The 0.4.0 release drives an
ATI Mach64 VT2 through exactly that route, on a package whose INF does not list
the card, with the framebuffer address taken entirely from the video BIOS.

If it does not work, the failure should be legible rather than mysterious:
`C:\V9XBOOT.INI` records how far the driver got and a `VbeDetail` key says which
VESA step refused. Send that.

### Helping add native support

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
  and screen-to-screen copies.
- **Measured, not asserted:** on Ironfield RTS's `BltFast` presentation path the
  retail S3 driver gets 19 FPS and Velocity9x gets 18 — about 6% behind, with the
  same game binary on the same emulated ViRGE. Details and caveats in
  [docs/decisions/2026-08-17-native-driver-benchmark.md](docs/decisions/2026-08-17-native-driver-benchmark.md).

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

The result is `build/floppy`, about 700 KB, carrying **every** package so the
target machine can pick the one that matches its card — which matters offline,
because the card cannot be identified from the build host and a second trip is
expensive:

```
README.TXT     what this is, which folder to use, install and recovery
RECOVER.TXT    recovery steps, at the root so they are findable in a hurry
S3\            S3 ViRGE/DX 86C375 and Trio32/64 86C764  (5333:8A01, 5333:8811)
ATI\           ATI Mach64 VT2 and Rage Mobility-M       (1002:5654, 1002:4C4D)
VBE\           generic VESA 2.0, for anything else
```

Copy the whole tree to a formatted floppy, or to any other medium the machine
can read. Nothing is archived, because Windows 98 has no built-in extractor
and an offline machine may have no unzip tool — the files are usable straight
off the disk. Add `-Zip` if you want an archive for network transfer instead.

Each package carries a `SHA256.TXT`; after copying you can confirm nothing was
corrupted in transit.

## Reporting problems

Include the chip and PCI ID, the package build identifier, the display mode in
use, and the contents of `C:\V9XBOOT.INI` — its `Stage` key names the furthest
step the driver reached, and on the `VBE\` and `ATI\` packages a `VbeDetail` key
names which VESA step refused. `C:\V9XHW.INI` carries the detected adapter,
memory and stride. If DirectDraw or Direct3D is involved, add `C:\V9XDD.INI` and
`C:\V9XTRACE.INI`. If the machine failed to reach the desktop, a COM1 serial
capture is the most useful single artefact — [docs/INSTALL.md](docs/INSTALL.md)
explains how to set one up.

One thing worth reporting even when the driver claims success: **if the desktop
is visibly wrong — shredded, repeated, wrong colours — say so.** The driver's own
tests all run through GDI, so they agree with whatever the driver decided and
cannot see a display the hardware is scanning out incorrectly. Your eyes are
currently the only check that covers that.

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

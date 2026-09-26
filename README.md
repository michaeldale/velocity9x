# Velocity9x

A from-scratch display driver for Windows 98 with **hardware Direct3D and
OpenGL**. Its flagship target is the **Intel GMA 950**, where one driver
package gives the chip DirectDraw on its blitter, a Direct3D HAL on its
Gen3 3D engine, and an OpenGL 1.1 driver drawing through the same engine.
It also drives the S3 ViRGE/DX and Trio3D/2X with hardware Direct3D and
OpenGL, the S3 Trio32/64 with 2D acceleration, and ATI Mach64/Rage and
generic VESA cards through their video BIOS.

It is written against the Windows 98 DDI, DIB Engine, DirectDraw HAL,
Direct3D HAL and OpenGL ICD contracts, rather than derived from anyone's
driver sources.

![Quake 2 on the Intel GMA 950 through Velocity9x's OpenGL driver, at the
first map's start](docs/images/quake2-gma950-opengl-2026-09-26.png)

*Quake 2 through Velocity9x's OpenGL driver on an Intel GMA 950 (HP Mini
110 netbook, Atom N280), 28 fps at the first map's start.*

**Current version: 0.9.0, the OpenGL release** — see the
[changelog](CHANGELOG.md). Download it from
[releases/0.9.0](releases/0.9.0/README.md).
See [current status](docs/STATUS.md) for what is verified where and what
is open.

> Every GMA 950 result here comes from **one physical machine**, an HP
> Mini 110 (945GSE). Coverage differs by card and feature; read the
> [verification matrix](docs/STATUS.md) and the
> [installation guide](docs/INSTALL.md), and keep a cold backup before
> installing an engineering driver.

## Start here

| If you want to | Go to |
|---|---|
| Download a built driver or the survey tool | [releases/](releases/README.md) |
| Find out whether your card is supported | [Supported cards](#supported-cards) |
| Install it on a test VM or machine | [docs/INSTALL.md](docs/INSTALL.md) |
| Try it on a card that is not listed | [Have an unsupported card?](#have-an-unsupported-card) |
| See how it compares to S3's own driver | [How it compares](docs/comparisons.md) |
| Get it onto a machine with no network | [Transfer disk](#transfer-disk) |
| Recover a machine that will not boot | [packaging/win98se/RECOVER.TXT](packaging/win98se/RECOVER.TXT) |
| Build it from source | [docs/BUILDING.md](docs/BUILDING.md) |
| Help add support for your chip | [Helping add native support](#helping-add-native-support) |
| Understand the design | [docs/specifications/win9x-driver-boundaries.md](docs/specifications/win9x-driver-boundaries.md) |
| See what changed | [CHANGELOG.md](CHANGELOG.md) |

## Intel GMA 950

The `intel-gma` package drives the **GMA 950 on the 945GSE**
(`8086:27AE`). The video BIOS sets the modes (640x480 and the panel's
native 1024x576, at 8 and 16 bpp); the driver owns everything above that:

- **Direct3D** on the Gen3 3D engine, submitted through its ring:
  RGB565, ARGB1555 and ARGB4444 textures with mip chains laid out by the
  HAL, modulate and decal, Gouraud and flat shading, texture wrap, the
  application's alpha test, a 16-bit Z buffer with the application's
  comparison, and colour and alpha blending.
- **DirectDraw** fills and copies on the Gen3 blitter, and hardware page
  flipping.
- **OpenGL 1.1** through `V9XGL.DLL`, drawing on the same engine:
  textures of any power-of-two shape down to one texel sampled by the GPU,
  least-recently-used eviction when the part's few megabytes of video
  memory run out, and anything the GPU cannot express drawn by the CPU
  into the same frame.

Measured on the netbook:

| Application | API | Result |
|---|---|---|
| 3DMark 99 | Direct3D | 717 at 640x480 and 643 at 1024x576 (0.8.1); 669 at 1024x576 in 0.9.0 |
| 3D WinBench 98 quality suite | Direct3D | Runs to the end: culling, wrap, decal, mirror, add and modulate pass |
| Half-Life | Direct3D | Runs, alpha-tested fences see-through; presents on the blitter in 0.6 ms |
| Final Reality | Direct3D | Renders textured with depth testing and hardware flips |
| Quake 2 demo | OpenGL | 28 fps at demo1's start (`timerefresh`, 640x480); fullscreen and `vid_restart` work |
| Serious Sam: The First Encounter | OpenGL | Renders; 1.78 million triangles drawn by the GPU in a demo session, none refused |

![Serious Sam: The First Encounter on the Intel GMA 950 through
Velocity9x's OpenGL driver](docs/images/serious-sam-gma950-opengl-2026-09-26.png)

What is open on the GMA 950: GDI drawing still uses the CPU, the CPU waits
for the GPU after every batch ([plan](docs/plans/intel-gen3-async-submission.md)),
and Serious Sam's textures outgrow the part's free video memory, so copies
are evicted and uploaded again. The records behind each number are linked
from the [changelog](CHANGELOG.md).

## Direct3D

Velocity9x publishes a Direct3D HAL through DirectDraw, so DirectX games
see a hardware device. Three engines sit behind it:

- **Intel Gen3** on the GMA 950 — above.
- **S3D** on the ViRGE/DX and Trio3D/2X — a deliberately narrow but real
  hardware path: textured, Gouraud-shaded, perspective-correct triangles
  with mipmapping, trilinear filtering (bilinear on the Trio3D/2X), alpha
  blending, colour-key transparency, specular highlights, fog and Z
  testing, with depth clears on the blitter. It picks a matching 5:5:5
  High Color desktop automatically. The S3D has no additive or
  multiplicative blend, so some 3DMark 99 tests draw plainer
  ([record](docs/decisions/2026-09-23-the-tunnel-texture-has-no-checker.md)).
- **A CPU rasterizer** for cards without a 3D engine — on by default in
  the generic VESA package at 16 bpp, opt-in on S3 and ATI. Nothing has
  measured it on a period machine; expect it to be slow.

A **Direct3D mode selector** on the Velocity9x page in Display Properties
chooses the chip's engine, the CPU rasterizer, or none. With none, the
driver advertises no Direct3D, so DirectDraw enumerates no hardware device
and applications fall back to Microsoft's software rasterizers — useful
with a second graphics card, or when a game misbehaves. It takes effect
after a restart.

## OpenGL

New in 0.9.0: an **OpenGL 1.1 installable client driver**, `V9XGL.DLL`.
Windows' own OPENGL32.DLL finds it through the display driver and loads
it in place of Microsoft's software renderer, so any OpenGL program uses
it without configuration. The driver package installs and registers it.

It offers a double-buffered 16-bit colour, 16-bit depth pixel format on a
High Color desktop the card can draw into — 565 or 555, and 555 only on
the ViRGE. Elsewhere OPENGL32 serves its own generic formats, as it would
without it. It draws through the same render core and engines as
Direct3D:

- **GMA 950**: the GPU samples the textures; the CPU draws what the GPU
  cannot, into the same frame. Quake 2 and Serious Sam run.
- **ViRGE**: square textures in the S3D's own formats, and nothing it
  cannot draw exactly. Non-square textures and blends other than source
  alpha over its inverse are refused rather than drawn wrong, so the
  Quake 3 demo's menu draws its model but not its text.
- **CPU rasterizer**: every fragment operation the drivers implement,
  perspective-correct mip-mapped textures, any blend, the alpha test and
  fog.

Implemented: the matrix stacks, immediate mode, vertex arrays, clipping
(the scissor included), texture objects and images with the OpenGL 1.1
environments, state queries, `glReadPixels`, and front-buffer drawing.
Not yet: lighting, display lists, evaluators, feedback and selection, and
the rest of OpenGL 1.1 — the
[requirements inventory](docs/plans/opengl-1.1-requirements.md) tracks
every entry point. The driver writes `C:\V9XDIAG\V9XGL.LOG`, including
per-path counters every ten seconds, for bring-up.

## Supported cards

Cards are grouped into *families*, one built package each. A family's driver
binary serves every chip in it and picks the right one by PCI id at boot.

| | **Intel GMA 950** | **S3 ViRGE/DX** | **S3 Trio32/64** | **ATI Mach64 / Rage** | **Generic VESA** |
|---|---|---|---|---|---|
| PCI ID | `8086:27AE` (945GSE) exactly | `5333:8A01`, plus `8A13` (Trio3D/2X) | `5333:8811`, plus `8810`, `8812`, `8813`, `8814`, `8901` | `1002:5654`, `1002:4C4D` | `1234:1111`, or anything via Have-Disk |
| Package | `build/win98se-intel-gma` | `build/win98se-s3` | `build/win98se-s3` | `build/win98se-ati` | `build/win98se-vbe` |
| Status | Hardware Direct3D and OpenGL, verified on one physical netbook | Primary S3 target, hardware Direct3D and OpenGL | Conservative baseline, verified on 2 physical machines | Tier-0 bring-up | Tier-0 fallback, verified on a physical GMA 950 and an S3 Trio3D |
| Display modes | 640x480 and native 1024x576 at 8 and 16 bpp, set by the video BIOS | 640x400x8; 640/800/1024 at 8, 16 and 32 bpp; 1280x1024 at 8 and 16 bpp | same, subject to BIOS and VRAM | 640x400x8, 640/800/1024 at 8 and 16 bpp; see Mach64 caveat below | baseline as ATI, plus validated modes from the BIOS |
| Direct3D | Yes (Gen3 3D engine) | Yes (narrow S3D path) | Software rasterizer, opt-in | same as Trio | Software rasterizer, **on by default** at 16 bpp |
| OpenGL (`V9XGL.DLL`) | Yes, GPU textures plus CPU fallback, at 16 bpp | Yes, S3D-expressible draws, 555 desktop | Software rasterizer at 16 bpp | Software rasterizer at 16 bpp | Software rasterizer at 16 bpp |
| Direct3D mode selector | Hardware / Software / Disabled | Hardware / Software / Disabled | Software / Disabled | same as Trio | same as Trio |
| DirectDraw surfaces / vblank | Yes | Yes | Yes | Yes | Yes |
| Hardware primary page flip | Yes (through the ring) | Yes | Yes | No; HAL declines | No; HAL declines |
| Hardware colour fill | DirectDraw only (Gen3 blitter) | Yes (S3D) | Yes (8514/A) | **No** — CPU | **No** — CPU |
| Hardware BitBLT | DirectDraw only, non-overlapping (Gen3 blitter) | Yes (S3D) | Yes (8514/A) | **No** — CPU | **No** — CPU |
| GDI acceleration by default | Software; the blitter is not yet used for GDI | Solid fill + screen copy (S3D) | Solid fill + screen copy (8514/A) | Software; no native backend yet | Software; generic BIOS path |
| Live resolution change | Yes, as games switch 1024x576 to 640x480 | Yes | Yes | Yes | Yes |
| Live colour-depth change | Not recorded | Yes | Yes | Yes | Yes |
| Hardware cursor | No | No (software cursor) | No | No | No |

The OpenGL row for the cards without a 3D engine is where the software
rasterizer is selected; OpenGL on those has not been measured on a
physical machine. On any card the ICD needs a High Color desktop.

The five ids after `8811` are **aliases**: parts the Trio64's code drives
unchanged, bound so they install, but validated nowhere. Only `8901`
(Trio64V2/DX) was even confirmed as an id here, off an option ROM. Treat them
as "it should come up", not as supported; see
[docs/specifications/family-manifest.md](docs/specifications/family-manifest.md)
and [docs/decisions/2026-08-29-s3-device-id-survey.md](docs/decisions/2026-08-29-s3-device-id-survey.md).
The Trio32 86C732 and the Trio64V+ 86C765 publish `8811` itself, and are
measured on an 86Box Trio32 guest
([record](docs/decisions/2026-08-29-s3-trio32-alias-guest.md)).

The **Matrox Millennium II** family (`102B:051B`) uses a guarded drop-in
package. Its historical mixed pair — Velocity9x's display driver with the
board's stock Matrox mini-VDD — passed physical software-GDI tests at
640x480x16 and 1024x768x16. That evidence does not validate the current release
archive or replacement of the stock mini-VDD; see the
[bring-up boundary](docs/specifications/matrox-millennium2-bringup.md).

### 2D and DirectDraw

- **Display modes** — 640x480, 800x600 and 1024x768 at 256 colours and
  High Color, plus 640x400 at 256 colours; on the S3 targets also True
  Color at those three and 1280x1024 at 256 colours and High Color.
  Resolution *and* colour-depth changes apply live on the ViRGE. The
  generic VESA family asks the video BIOS which modes it really has every
  boot and merges the drivable ones in, and the panel's EDID picks the
  fallback mode.
- **2D output** — through the system DIB Engine, with the framebuffer
  mapped linearly. On both S3 chips, GDI solid fills and screen-to-screen
  copies (window moves and scrolls, including overlapping copies in all
  eight directions) run on the 2D engine, with a DIB Engine fallback and a
  latch that turns acceleration off for the session after a detected
  timeout. Text and monochrome uploads are opt-in (`GdiAccelText=1`,
  `GdiAccelUpload=1`); physical Trio64 text validation remains open.
- **DirectDraw** — a flat 32-bit HAL (`V9XHAL.DLL`) providing video-memory
  surfaces and vertical-blank services; fills and blits on the chip's 2D
  engine where there is a backend (both S3 parts, the GMA 950) and on the
  CPU where there is not.
- **A Velocity9x page inside Display Properties** reporting the detected
  adapter, PCI ID, video memory, active mode and clock, which acceleration
  paths are live, and the driver's runtime diagnostics.

![The Velocity9x page in Windows 98 Display Properties, showing an S3 ViRGE/DX
at 800x600x16 with the linear aperture mapped and a passing GDI test](docs/images/velocity9x-display-properties.png)

### Verified on physical hardware: S3 Trio64 on PCI

0.4.2 was the first release proven on a real card rather than an emulator.
The full stack — the driver, its DirectDraw HAL and its own mini-VDD — runs
on a physical **S3 Trio64 (86C764, 2 MB, Windows 98 SE)**: desktop at
1024x768x16, 32-bpp modes at 640x480 and 800x600, hardware fills and blits on
the 8514/A engine, CRTC page flipping and real vertical-blank services. It is
faster than S3's own Windows 98 driver there in Ironfield RTS at 640x480
fullscreen:

| Presentation path | Velocity9x | Stock S3 |
|---|---|---|
| Direct back buffer | **27 FPS** | 25 FPS |
| Video memory + `BltFast` | **27 FPS** | 23 FPS |
| System RAM | 20 FPS | 22 FPS |
| Windowed | 18 FPS | 18 FPS |

With GDI acceleration, CrystalMark Retro 2.1.0's Square and Image scores
rose from 253 to 275 and 91 to 98 on the same card, with the unaccelerated
Circle test unchanged as a control
([record](docs/decisions/2026-08-27-crystalmark-barry-accelerated.md)).
Two bugs only real silicon exposed — a 4 MiB video-memory assumption and a
misaligned mini-VDD scratch buffer — are written up in
[docs/issues/](docs/issues/).

### Verified on physical hardware: S3 Trio64 on VESA Local Bus, under Windows 95

A **486 with an S3 Trio64 on VESA Local Bus, running Windows 95 4.00.950**:
installed by hand from a model with no hardware ID, identifying the chip from
the S3's own registers, at 640x480x8 with the aperture at `0x7F000000`. The
model names no mini-VDD, because ours does not load on Win95 and a display
devnode whose mini-VDD fails is one Windows never enables
([handover](docs/handoffs/2026-08-22-vlb-manual-select-handover.md)).

### Tier-0: how a new card starts

The ATI and generic VESA columns are **tier-0**: the mode is set through the
VESA BIOS, the framebuffer address comes from the BIOS, and the CPU does all
the drawing. No chip register is touched, so the same code drives any card
with a VESA 2.0 BIOS and a linear framebuffer — it drove the GMA 950 on the
first attempt, at the panel's native 1024x576
([findings](docs/issues/2026-08-27-netbook-gma950-findings.md)), before the
chip had its own package. A native backend adds acceleration later; on
Ironfield's `BltFast` path tier-0 on a Mach64 gets 6 FPS where the ViRGE's
blitter gets 18.

**Known issue:** 16 bpp modes display incorrectly on the Mach64 — 8 bpp is
correct at every resolution
([D5](docs/issues/2026-08-16-tier0-defects-deferred.md)).

## Have an unsupported card?

**Try the `VBE\` package.** Tier-0 needs nothing but a VESA 2.0 BIOS and a
linear framebuffer, so it stands a fair chance on a card nobody here has seen.
Its INF lists one PCI id, because that is all anyone has tested — to use it on
anything else, pick it explicitly through **Have Disk** in the Display
Properties adapter dialog and accept the "not intended for this hardware"
warning. Windows should never bind this driver automatically to a card it was
not verified on, but you should be able to choose it.

If it does not work, the failure should be legible rather than mysterious:
`C:\V9XDIAG\V9XBOOT.INI` records how far the driver got and a `VbeDetail` key
says which VESA step refused. Send that.

### Helping add native support

Writing a backend for a chip means knowing what is actually on the board.
`V9XSURV.EXE` collects it: the PCI identifiers and configuration space, the
video BIOS, the VBE mode list, your monitor's EDID and the raw VGA register
file.

**Download it from [releases/](releases/README.md)** — the newest version
folder holds `velocity9x-survey-<version>.zip`. It is a real-mode DOS
program: boot to DOS (`Start` → `Shut Down` → *Restart in MS-DOS mode*), run
`V9XSURV` (add `/rom` to include the video BIOS image), and send back the
`C:\V9XDIAG\V9XSURV.INI` it writes. A DOS box inside Windows also works; it
just sees less.

It reads, it does not write: no mode change, nothing installed, nothing left
behind. The one step that writes anything is the opt-in vendor probe, which
sets documented unlock keys for your chipset family, reads the registers
behind them and restores the originals; it is asked as a question, after the
main report is on disk. The report is plain text; the one identifying item in
it is your monitor's EDID, which carries its model and serial number. What it
captures and why is in
[docs/specifications/vga-survey.md](docs/specifications/vga-survey.md), and
reports are decoded by
[scripts/parse-vga-survey.ps1](scripts/parse-vga-survey.ps1).

## Transfer disk

For a machine with no network, build one 1.44 MB floppy per family:

```powershell
./scripts/build-floppy-package.ps1
```

Each disk is `build/floppy/<family>`, about 810 KB since the OpenGL driver
joined the packages:

```
README.TXT     what this disk is, which chips it serves, install and recovery
RECOVER.TXT    recovery steps, at the root so they are findable in a hurry
S3\            the family's package (here the S3 disk)
```

Copy a disk's tree to a formatted floppy or any medium the machine can read.
Nothing is archived, because Windows 98 has no built-in extractor; add `-Zip`
for one archive per disk for network transfer instead. Each package carries a
`SHA256.TXT` to confirm nothing was corrupted in transit.

## Common questions

**Will my Direct3D games work?**
Compatibility is title-specific. On the GMA 950, 3DMark 99, 3D WinBench 98's
quality suite, Half-Life and Final Reality run. On the S3 hardware path,
Final Reality and 3DMark 99 run, some 3DMark tests plainer for the S3D's
missing blends; Incoming refuses its texture formats. Use the mode selector's
Disabled setting to let a game fall back to Microsoft's rasterizers.

**Will my OpenGL games work?**
On the GMA 950, Quake 2 and Serious Sam: The First Encounter run. On the
ViRGE, only draws its S3D can express exactly are drawn, so games that rely
on non-square textures or additive and multiplicative blends — the Quake 3
demo among them — are missing much of their picture. Lighting, display lists
and the rest of OpenGL 1.1 are not implemented yet, so a program that needs
them will draw wrongly or not at all.

**Will it run on Windows 95 or Windows Me?**
Windows 98 SE is the main target. The manual VLB install is verified on one
physical Trio64 under Windows 95, without the mini-VDD; that does not
establish general Win95 compatibility. Windows Me has no validated
configuration.

**Can I get 32-bit colour, or a resolution above 1024x768?**
On the S3 targets, yes: True Color at 640x480, 800x600 and 1024x768, and
1280x1024 at 256 colours and High Color, memory permitting — 1024x768 at 32
bpp needs 3 MB. The GMA 950 offers only its video BIOS's modes. Generic VESA
can add True Color and other resolutions from the BIOS mode list. There are
no packed 24-bpp modes.

**Can I run this on real hardware, or only in an emulator?**
Both. Physical evidence covers the GMA 950 (one netbook), the ViRGE/DX and
Trio3D/2X, and the Trio64 on PCI and VLB. Matrox has historical physical
software-GDI evidence with its stock mini-VDD; ATI remains emulator-only.
Development and regression testing use [86Box](https://86box.net/) and QEMU.
Read [docs/INSTALL.md](docs/INSTALL.md) first and have a recovery path.

**Do I uninstall the existing display driver first?**
No — and do not remove the display adapter in Device Manager either, because
Windows re-detects it and installs Microsoft's in-box driver instead of this
one. Install over the top with **Have Disk**, per step 6 of
[docs/INSTALL.md](docs/INSTALL.md).

**My card is not in the table. Is it hopeless?**
No. Try the `VBE\` package through Have Disk — see
[Have an unsupported card?](#have-an-unsupported-card).

## Reporting problems

Include the chip and PCI ID, the package build identifier, the display mode in
use, and `C:\V9XDIAG\V9XBOOT.INI` — its `Stage` key names the furthest step
the driver reached. `C:\V9XDIAG\V9XHW.INI` carries the detected adapter,
memory and stride. For DirectDraw or Direct3D add `C:\V9XDIAG\V9XDD.INI` and
`C:\V9XDIAG\V9XTRACE.INI`; for OpenGL add `C:\V9XDIAG\V9XGL.LOG`. If the
machine failed to reach the desktop, a COM1 serial capture is the most useful
single artefact — [docs/INSTALL.md](docs/INSTALL.md) explains how.

**If the picture is visibly wrong — shredded, repeated, missing, wrong
colours — say so,** even when the driver reports success, and include a
screenshot or photograph.

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

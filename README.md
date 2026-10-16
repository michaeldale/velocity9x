# Velocity9x

A from-scratch display driver for Windows 98 with **hardware 3D on period
cards**. One driver package per chip family gives the card:
- DirectDraw on its 2D engine;
- a Direct3D HAL on its 3D engine;
- an OpenGL 1.1 driver;
- Glide 2 and Glide 3 for games written for 3dfx cards;
- ATI's 3DCIF for games written for ATI Rage cards.

All of them draw through one shared render core. It runs the Intel GMA 950,
the ATI Rage Pro, Rage XL, Rage Mobility and Rage IIC, the S3 ViRGE and
Trio3D, the Matrox Millennium II, the SiS 6326 and the 3dfx Voodoo4 with
hardware 3D. The S3 Trio32/64 and the Matrox Millennium get 2D
acceleration, and other cards run through their VESA video BIOS.

It is written against the Windows 98 DDI, DIB Engine, DirectDraw HAL,
Direct3D HAL and OpenGL ICD contracts, rather than derived from anyone's
driver sources.

![Quake 2 on the Intel GMA 950 through Velocity9x's OpenGL driver, at the
first map's start](docs/images/quake2-gma950-opengl-2026-09-26.png)

*Quake 2 through Velocity9x's OpenGL driver on an Intel GMA 950 (HP Mini
110 netbook, Atom N280), 28 fps at the first map's start.*

**Current version: 0.18.5.** It makes the first ATI 3DCIF games draw:
Assault Rigs and Wipeout get their textures, lines are drawn, Formula 1's
keyed textures are no longer refused on the Rage Pro class, and ATI cards
run every 16-bit mode as 5:5:5. See the [changelog](CHANGELOG.md), and
download it from [releases/0.18.5](releases/0.18.5/README.md). Installed copies from
0.15.0 on can update through **Check for updates** on the Velocity9x
tab. [Current status](docs/STATUS.md) has what is verified where and what
is open.

> This is an engineering driver. Each card's results come from **one
> physical machine**: two for the Trio64, none for a card marked
> "claimed". Read the [installation guide](docs/INSTALL.md), and keep a
> cold backup before installing.

## Start here

| If you want to | Go to |
|---|---|
| Download a built driver or the survey tool | [releases/](releases/README.md) |
| Find out whether your card is supported | [Supported cards](#supported-cards) |
| Install it on a test VM or machine | [docs/INSTALL.md](docs/INSTALL.md) |
| Try it on a card that is not listed | [Have an unsupported card?](#have-an-unsupported-card) |
| See how it compares to S3's own driver | [How it compares](docs/comparisons.md) |
| Get it onto a machine with no network | [Transfer disks](#transfer-disks) |
| Recover a machine that will not boot | [packaging/win98se/RECOVER.TXT](packaging/win98se/RECOVER.TXT) |
| Report a problem | [Reporting problems](#reporting-problems) |
| Build it from source | [docs/BUILDING.md](docs/BUILDING.md) |
| Run the standard Half-Life 1 benchmark | [HL1 benchmark](tests/benchmarks/hl1/README.md) |
| Help add support for your chip | [Helping add native support](#helping-add-native-support) |
| Understand the design | [docs/specifications/win9x-driver-boundaries.md](docs/specifications/win9x-driver-boundaries.md) |
| See what is verified and what is open | [docs/STATUS.md](docs/STATUS.md) |
| See what changed | [CHANGELOG.md](CHANGELOG.md) |

## Supported cards

Cards are grouped into *families*, one package each. A family's driver
serves every chip in it and picks the right one by PCI id at boot.
"Measured" means run on the physical machine named; a card that is only
"claimed" binds by design and has not been run.

| Card | Package | 3D engine | Measured on hardware |
|---|---|---|---|
| Intel GMA 950 (945GSE, `8086:27AE`) | `intel-gma` | Gen3 | Direct3D, OpenGL, Glide 2 and 3, 3DCIF; DirectDraw on the blitter. One HP Mini 110 netbook; a reporter runs 3D on a Nexcom NISE 3270 with an external VGA monitor |
| Intel GMA 900 (915GM, `8086:2592`) | `intel-gma` | Gen3 | Not run here. A reporter runs Direct3D on one Lenovo laptop |
| ATI Rage XL, Rage Pro, Rage Mobility (Rage Pro class) | `ati` | Mach64 | Rage XL PCI: Direct3D (including DirectX 8), OpenGL, Glide 2 and 3, 3DCIF. Rage Mobility-M: Direct3D and OpenGL, on one Gateway laptop |
| ATI Rage IIC and the Rage II class | `ati` | Rage II, setup on the CPU | Rage IIC AGP: Direct3D, OpenGL, Glide 2 and 3, 3DCIF |
| ATI Mach64 VT2 | `ati` | none | 2D through the BIOS, on an emulator |
| S3 ViRGE/DX, Trio3D/2X, Trio3D | `s3` | S3D | Trio3D/2X and Trio3D: Direct3D (a narrow path). ViRGE/DX: GDI on the engine; its Direct3D and OpenGL on an emulator |
| S3 Trio32/64 | `s3` | none | GDI and DirectDraw on the 8514/A engine, on PCI and on a VLB 486 under Windows 95 |
| Matrox Millennium II (MGA-2164W) | `matrox` | MGA | Direct3D; GDI and DirectDraw on the engine |
| Matrox Millennium (MGA-2064W) | `matrox` | none | GDI (fills, copies, text), DirectDraw and page flips on the engine |
| SiS 6326 | `sis` | SiS 3D | Direct3D, OpenGL; DirectDraw on the engine |
| 3dfx Voodoo4 4500 (VSA-100) | `3dfx` | VSA-100 | Direct3D, OpenGL, and 3dfx's own Glide; GDI on the engine |
| Anything with a VESA 2.0 BIOS | `vbe` | none: software rasterizer | Tier-0 on a GMA 950 and a Trio3D, through Have Disk |

Unvalidated **aliases** bind to the measured chip of the same design: the
other S3 Trio ids, the rest of the Rage II and Rage Pro lines, and the
Voodoo5, which would run on one chip. Treat them as "it should come up",
not as supported. The ids are listed in each family's manifest under
[packaging/families/](packaging/families/), and why each was bound in
[docs/specifications/family-manifest.md](docs/specifications/family-manifest.md).

Modes come from each card's video BIOS, merged with the driver's own where
it sets modes itself: up to 1024x768 at 8 and 16 bpp on most cards, True
Color and 1280x1024 on the S3 cards, and 1024x576 on the GMA 950's panel.
Each package's `INSTALL.TXT` lists them. GDI is drawn by the CPU except
where the table says otherwise. No card has a hardware cursor.

### Measured frame rates

Recorded on each card as it shipped, at 640x480. They are not a
like-for-like comparison: the machines differ, and so do the versions.

| Card | Half-Life `mwd5`, Direct3D | Quake 2, OpenGL | Version |
|---|---|---|---|
| Intel GMA 950 (Atom N280) | 42.4 fps | 19.6 fps (demo2) | 0.9.2 |
| 3dfx Voodoo4 4500 (P3) | 25.7 fps | 20.7 fps (`timerefresh`) | 0.17.0 |
| ATI Rage XL PCI (P3) | 18.1 fps | 11.6 fps | 0.10.1 |
| ATI Rage Mobility-M (P3 laptop) | 12.0 fps | about 8 fps | 0.9.2 |
| SiS 6326 (P3) | 8.6 fps | 7.8 fps (`timerefresh`) | 0.11.0 |
| Matrox Millennium II (P3) | 8.49 fps | | 0.16.0 |
| ATI Rage IIC (P3) | 5.57 fps (ATI's driver 6.63) | 4.4 fps | 0.10.0 |

The records behind each number are linked from the
[changelog](CHANGELOG.md).

## Direct3D

Velocity9x publishes a Direct3D HAL through DirectDraw, so DirectX games
see a hardware device. Behind it sit the chips' own 3D engines, listed in
the table above, and a **CPU rasterizer** for cards without one. The
rasterizer is on by default in the `vbe` package at 16 bpp and opt-in
elsewhere; nothing has timed it on a period machine, so expect it to be
slow.

Each engine draws what it was measured to draw exactly. A draw an engine
cannot express is refused rather than drawn wrong. On the GMA 950 a
refused draw falls back to the CPU, into the same frame. Elsewhere it is
not drawn: see each card's records for what that leaves out. The S3D
in particular has no additive or multiplicative blend, so some 3DMark 99
tests draw plainer
([record](docs/decisions/2026-09-23-the-tunnel-texture-has-no-checker.md)).

A **Direct3D mode selector** on the Velocity9x Advanced tab chooses the
chip's engine, the CPU rasterizer, or none. With none, the driver
advertises no Direct3D, so applications fall back to Microsoft's software
rasterizers. That is useful with a second graphics card, or when a game
misbehaves. It takes effect after a restart.

### Direct3D 8 games (DDI 6)

DirectX 8 games need a newer driver interface than earlier games use,
DrawPrimitives2 (DDI 6). Without it, Direct3D 8 finds no hardware device
and games fail with "D3DERR_NOTAVAILABLE" (`0x8876086A`). Velocity9x
offers DDI 6 to the programs that need it and keeps the older interface
for the rest, which is faster for DirectX 5 to 7 games.

The **DDI 6** selector on the Velocity9x Advanced tab sets this, and takes
effect the next time a program starts, with no restart:

- **Automatic** (the default): DDI 6 for any program that uses Direct3D 8,
  the older interface for everything else.
- **Never**: the older interface for every program. Direct3D 8 games then
  get no hardware device.
- **Always**: DDI 6 for every Direct3D program.

The same choice in `SYSTEM.INI`, `[Velocity9x]` section: no
`Direct3DDdi` line for Automatic, `Direct3DDdi=5` for Never,
`Direct3DDdi=6` for Always. One program can be set on its own in a
section of its own, by file name, `5` or `6`:

```ini
[Velocity9x.Direct3DDdi]
HL.EXE=6
```

What works so far, measured on the Rage XL and the GMA 950: DxDiag's
Direct3D 8 test, and 3DMark 2001 SE to a full score (111 and 244 3DMarks
at 640x480; its game tests are slow on cards of this age). Direct3D 8 is
offered 16-bit colour only: 16-bit display modes, 16-bit textures and a
16-bit depth buffer, with no stencil. A game that insists on 32-bit
colour, compressed textures or stencil will not start or will drop the
feature. Direct3D 9 needs a newer interface still and is not available.

## OpenGL

`V9XGL.DLL` is an **OpenGL 1.1 installable client driver**. Windows' own
OPENGL32.DLL finds it through the display driver and loads it in place of
Microsoft's software renderer, so any OpenGL program uses it without
configuration. The driver package installs and registers it.

It offers a double-buffered 16-bit colour, 16-bit depth pixel format on a
High Color desktop the card can draw into, 565 or 555. It draws through
the same engines as Direct3D, and on the GMA 950 the CPU draws what the
GPU cannot. Quake 2, Half-Life, Serious Sam: The First Encounter and UT99
run on one card or another.

Implemented: the matrix stacks, immediate mode, vertex arrays, clipping
(the scissor included), texture objects and images with the OpenGL 1.1
environments, fog, attribute stacks, state queries, `glReadPixels`,
front-buffer drawing, and `GL_SGIS_multitexture` on the Rage Pro and
Gen3. Not yet: lighting, display lists, evaluators, and feedback and
selection. The [requirements inventory](docs/plans/opengl-1.1-requirements.md)
tracks every entry point. The driver writes `C:\V9XDIAG\V9XGL.LOG`.

## Glide

`GLIDE2X.DLL` and `GLIDE3X.DLL` give games written for 3dfx Voodoo cards
their Glide path on any card with a 3D engine, drawing through the same
render core. Every package except `3dfx` installs both to SYSTEM, but
never over a newer copy, so a Voodoo add-on card keeps its own. The
Voodoo4's package ships none: the card runs 3dfx's own Glide, and the
driver hands the card to it and takes it back.

Measured:
- **Need for Speed II SE:** races on the GMA 950.
- **Carmageddon II:** its Glide option runs, menus and races, on the
  GMA 950.
- **Diablo II** (shareware demo): to the town on the Rage XL and the
  Rage IIC.
- **Rollcage** (demo): menus and races on the Rage XL and the GMA 950.

A game that carries its own Glide DLL in its folder loads that first.
The logs are `C:\V9XDIAG\V9XGLIDE.LOG` and `V9XGLD3.LOG`.

## ATI 3DCIF

`ATI3DCIF.DLL` is ATI's 3D Rage interface: a handful of late-90s games and
demos were written for ATI Rage cards and draw through it, as Glide games
draw through Glide. ATI's own copy talks to ATI's driver and cannot run
on Velocity9x. Ours draws through the same render core, so 3DCIF also
runs on cards ATI's never supported, the Rage XL PCI and the GMA 950
among them. Every package installs it to SYSTEM over any copy already
there.

All eight of ATI's SDK 4.1 demos draw on the Rage IIC, the Rage XL and the
GMA 950, VQ-compressed textures included. A tester has tried Formula 1
and Tomb Raider on 0.18.0; both need more work. Not yet done:
- mipmaps;
- the Rage Pro's tiled texture layout;
- meshes and TLVERTEX vertices.

On the ATI cards a few blended sprites that need two kinds of
transparency at once are not drawn. Titles written for ATI cards assume
a 5:5:5 desktop and show wrong colours on 5:6:5 (see below): ATI's Cubes
demo clears blue instead of grey. The log is `C:\V9XDIAG\V9XCIF.LOG`,
and **Send report** includes it from 0.18.5.

## 16-bit colour: 5:6:5 or 5:5:5

High Color comes in two layouts. Most cards and games use 5:6:5. The S3
ViRGE's 3D engine draws only 5:5:5, and ATI's own driver ran 5:5:5, which
titles written for ATI cards assume. So on the ViRGE, and on ATI cards
with hardware Direct3D, the driver chooses 5:5:5 by itself. Glide games
that write the screen as 5:6:5 are converted.

The **16-bit colour** selector on the Velocity9x Advanced tab overrides
the choice, as does `[Velocity9x] HighColor=16` (or `15`) in
`SYSTEM.INI`. It takes effect after a restart.

## Display Properties

Velocity9x adds two tabs to Display Properties:
- **Velocity9x** reports the adapter, PCI id, video memory, the active
  mode and which acceleration paths are live, and has **Check for
  updates**.
- **Velocity9x Advanced** holds the Direct3D, DDI 6, vertical sync and
  16-bit colour selectors, shows the clocks, and has **Send report**.

Features the driver does not have yet are greyed out. `V9XSET.EXE` shows
the same status in its own window.

![The Velocity9x tab in Windows 98 Display Properties on an S3 ViRGE/DX at
1024x768x16: engine fill, copy and page flip, hardware Direct3D, and passing
GDI and DirectDraw tests](docs/images/velocity9x-display-properties.png)
![The Velocity9x Advanced tab: the Direct3D, DDI, vertical sync and 16-bit
colour selectors, greyed-out texture filtering and write combining, and a
56.079 MHz core and memory clock](docs/images/velocity9x-display-properties-advanced.png)

The screenshots are from the 86Box ViRGE/DX guest on build 2252df1; the
pages have gained the update and report buttons since.

## Where it started: the S3 Trio64

0.4.2 was the first release proven on a real card rather than an
emulator: a physical **S3 Trio64 (86C764, 2 MB)**. It was faster than S3's
own Windows 98 driver in Ironfield RTS at 640x480, 27 fps against 25 on
the direct back-buffer path, and GDI acceleration lifted CrystalMark
Retro's 2D scores
([record](docs/decisions/2026-08-27-crystalmark-barry-accelerated.md)).
Two bugs only real silicon exposed are written up in
[docs/issues/](docs/issues/). The same driver runs a Trio64 on VESA Local
Bus under Windows 95, installed by hand without the mini-VDD
([handover](docs/handoffs/2026-08-22-vlb-manual-select-handover.md)).
[How it compares](docs/comparisons.md) has the details.

## Have an unsupported card?

**Try the `vbe` package.** Tier-0 needs nothing but a VESA 2.0 BIOS and a
linear framebuffer: the mode is set through the BIOS, and the CPU does all
the drawing, so it stands a fair chance on a card nobody here has seen. It
drove the GMA 950 at its panel's native 1024x576 on the first attempt,
before that chip had its own package. Its INF lists one PCI id, because
that is all anyone has tested. To use it on anything else, pick it
explicitly through **Have Disk** in the Display Properties adapter dialog
and accept the "not intended for this hardware" warning.

If it does not work, the failure should be legible rather than mysterious:
`C:\V9XDIAG\V9XBOOT.INI` records how far the driver got and a `VbeDetail` key
says which VESA step refused. Send that.

### Helping add native support

Writing a backend for a chip means knowing what is actually on the board.
`V9XSURV.EXE` collects it: the PCI identifiers and configuration space, the
video BIOS, the VBE mode list, your monitor's EDID and the raw VGA register
file.

**Download it from [releases/](releases/README.md)**: the newest version
folder holds `velocity9x-survey-<version>.zip`. It is a real-mode DOS
program: boot to DOS (`Start` → `Shut Down` → *Restart in MS-DOS mode*), run
`V9XSURV` (add `/rom` to include the video BIOS image), and send back the
`C:\V9XDIAG\V9XSURV.INI` it writes. A DOS box inside Windows also works; it
just sees less.

It reads; it does not write. There is no mode change, nothing is
installed, and nothing is left behind. The one step that writes anything
is the opt-in vendor probe, which sets documented unlock keys for your
chipset family, reads the registers behind them and restores the
originals. It is asked as a question, after the main report is on disk.
The report is plain text; the one identifying item in it is your
monitor's EDID, which carries its model and serial number. What it
captures and why is in
[docs/specifications/vga-survey.md](docs/specifications/vga-survey.md), and
reports are decoded by
[scripts/parse-vga-survey.ps1](scripts/parse-vga-survey.ps1).

## Transfer disks

For a machine with no network, each family that has a floppy build gets
two 1.44 MB disks:

```powershell
./scripts/build-floppy-package.ps1
```

```
build/floppy/S3/       disk 1: README.TXT, RECOVER.TXT, and S3\ with the
                       INF and every file the install copies
build/floppy/S3-2/     disk 2: README.TXT, and S3\ with the test tools
```

Copy both disks' `S3\` folders into one folder on the hard disk, and
install from there. Nothing is archived, because Windows 98 has no
built-in extractor; add `-Zip` for one archive per disk for network
transfer instead. Each disk carries a `SHA256.TXT` of its own files to
confirm nothing was corrupted in transit. The `intel-gma` package has no
floppy build; use its zip.

## Common questions

**Will my Direct3D games work?**
Compatibility is title-specific. Half-Life runs on every card with a 3D
engine. 3DMark 99 runs on the GMA 950, the Rage XL, Rage Mobility-M and
Rage IIC, the SiS 6326 and the Trio3D/2X, and Final Reality on most of
them. On the S3 hardware path some 3DMark tests draw
plainer for the S3D's missing blends, and Incoming refuses its texture
formats. Use the mode selector's Disabled setting to let a game
fall back to Microsoft's rasterizers.

**Will my OpenGL games work?**
Quake 2 and Half-Life run on most cards with a 3D engine, and Serious Sam
on the GMA 950. On the ViRGE only draws its S3D can express exactly are
drawn, so games that rely on non-square textures or additive and
multiplicative blends, the Quake 3 demo among them, are missing much of
their picture. Lighting, display lists and the rest of OpenGL 1.1 are not
implemented yet.

**Will my Glide or ATI games work?**
See [Glide](#glide) and [ATI 3DCIF](#ati-3dcif) for what has run. Remove or
rename a game's own copy of the DLL from its folder so ours is used.

**Will it run on Windows 95 or Windows Me?**
Windows 98 SE is the main target. The manual VLB install is verified on one
physical Trio64 under Windows 95, without the mini-VDD; that does not
establish general Win95 compatibility. Windows Me has no validated
configuration.

**Can I get 32-bit colour, or a resolution above 1024x768?**
On the S3 cards, yes: True Color at 640x480, 800x600 and 1024x768, and
1280x1024 at 256 colours and High Color, memory permitting. 1024x768 at
32 bpp needs 3 MB. The Matrox Millennium also runs 32 bpp, and modes to
1600x1200. Other cards offer their video BIOS's modes; there are no
packed 24-bpp modes. The 3D paths are 16-bit.

**Can I run this on real hardware, or only in an emulator?**
Both. Every card in the [table](#supported-cards) marked as measured ran on
a physical machine. Development and regression testing also use
[86Box](https://86box.net/) and QEMU. Read
[docs/INSTALL.md](docs/INSTALL.md) first and have a recovery path.

**Do I uninstall the existing display driver first?**
No, and do not remove the display adapter in Device Manager either,
because Windows re-detects it and installs Microsoft's in-box driver
instead of this one. Install over the top with **Have Disk**, per step 6
of [docs/INSTALL.md](docs/INSTALL.md).

**My card is not in the table. Is it hopeless?**
No. Try the `vbe` package through Have Disk: see
[Have an unsupported card?](#have-an-unsupported-card).

## Reporting problems

Right after the program that went wrong, open Display Properties,
Settings, Advanced, the **Velocity9x Advanced** tab, and click **Send
report**. It runs `V9XTRACE.EXE`, lists the files it will send, and after
you click Send gives you a code such as `V9X-4F7K2Q`. Quote the code in
an issue at
[github.com/michaeldale/velocity9x/issues](https://github.com/michaeldale/velocity9x/issues).
Nothing is sent unless you click Send. If that machine has no network,
open http://michaeldale.com.au/v9update/report on any computer and attach
the files from `C:\V9XDIAG` there.

Also say the chip, the display mode in use and what the program did. **If
the picture is visibly wrong (shredded, repeated, missing, wrong
colours), say so,** even when the driver reports success, and include a
screenshot or photograph. If the machine failed to reach the desktop, a
COM1 serial capture is the most useful single artefact;
[docs/INSTALL.md](docs/INSTALL.md) explains how.

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

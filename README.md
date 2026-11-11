# Velocity9x

A from-scratch display driver for Windows 98 with **hardware 3D on period
cards**. One driver package per chip family gives the card:
- DirectDraw on its 2D engine;
- a Direct3D HAL on its 3D engine;
- an OpenGL 1.1 driver;
- Glide 2 and Glide 3 for games written for 3dfx cards;
- ATI's 3DCIF for games written for ATI Rage cards.

All of them draw through one shared render core. It runs these with
hardware 3D:
- **Intel:** GMA 900, GMA 950, GMA 3150, GMA X3100, HD Graphics
  (Ironlake), HD 4000 and HD 4600;
- **AMD and ATI:** Radeon HD 5450, HD 5770, HD 7470, HD 7660D, R3E
  (Mullins), R7E (Carrizo) and X1200; Rage 128 Pro, Rage Pro, Rage XL, Rage Mobility and
  Rage IIC;
- **others:** S3 ViRGE and Trio3D, Matrox Millennium II and Mystique, SiS
  6326, NVIDIA RIVA 128ZX and 3dfx Voodoo4.

The S3 Trio32/64 and the Matrox Millennium get 2D acceleration, and other
cards run through their VESA video BIOS. The Intel GMA 4500 and GMA X4500
were built from their surveys and run, by one reporter's account, on
0.28.0; the GMA 4500MHD likewise on 0.35.0, and from 0.44.0 on a tester's
machine through remote help. The GMA 3100 and Radeon
HD 5570 are recognised: built from a survey, not yet run.

It is written against the Windows 98 DDI, DIB Engine, DirectDraw HAL,
Direct3D HAL and OpenGL ICD contracts, rather than derived from anyone's
driver sources.

![Quake 2 on the Intel GMA 950 through Velocity9x's OpenGL driver, at the
first map's start](docs/images/quake2-gma950-opengl-2026-09-26.png)

*Quake 2 through Velocity9x's OpenGL driver on an Intel GMA 950 (HP Mini
110 netbook, Atom N280), 28 fps at the first map's start.*

## Current version: 0.46.0

[Download](releases/0.46.0/README.md) ·
[Changelog](CHANGELOG.md) ·
[What is verified and what is open](docs/STATUS.md). Installed copies from
0.15.0 on update through **Check for updates** on the Velocity9x tab.

New in 0.46.0:
- **`vbe` package: moving windows, scrolling and anything else that reads
  the screen is many times faster.** Windows draws the desktop into a copy
  in system memory and the driver copies each change to the screen. On a
  Radeon R3E reads went from 5 MB/s to 350-1,180 MB/s and scrolling an
  800x600 window from 400 ms to 2-3 ms; on an HD 4600, scrolling up a
  document is no longer seven times slower than scrolling down. Drawing
  that only writes costs a second copy: 0.3 to 0.8 of its old speed.
  `C:\V9XDIAG\NOSHADOW.TXT` turns it off
  ([record](docs/decisions/2026-11-11-vbe-screen-shadow-phase1.md),
  [AMD and other depths](docs/decisions/2026-11-11-vbe-screen-shadow-open-items.md)).

### Known issues

- **`vbe` package, software OpenGL in a window:** each frame's present
  reads a back buffer kept in video memory, so it is slow where the
  screen is slow to read (about 70 ms for a small window on a Radeon R3E,
  1.5 s at 800x600 on one GTX 970)
  ([issue](docs/issues/2026-11-11-vbe-soft-gl-back-buffer-in-video-memory.md)).
- **Full-screen DOS on cards other than the GMA 950, HD 4600 and S3
  ViRGE/DX:** the 0.45.0 fix is in every package but has not been run
  on them. If the desktop does not come back, keep DOS windowed.
- **Glide and 3DCIF DLLs from 0.23.0 or earlier** do not work with this
  driver. Replace any copy in a game's folder with this release's.
- **GMA 950:** 3DMark 2001 SE's Dragothic draws its sky white
  ([issue](docs/issues/2026-11-07-gma950-3dmark2001se-dragothic-sky-white.md)).
  Starting a Half-Life multiplayer game in OpenGL can leave the screen
  stuck; press Escape and resume
  ([issue](docs/issues/2026-10-26-gma950-hl-gl-multiplayer-start-stuck.md)).
- **HD 4600:** 3DMark 2001 SE's Nature draws its distant terrain white.
- **Radeon X1200:** 3DMark 2000's first game test tints some distant
  terrain red ([issue](docs/issues/2026-11-06-rs690-3dmark2000-pink-terrain.md)).
- **Radeon R3E:** the first OpenGL or Glide clear after a program starts
  sometimes times out and is skipped; later frames draw.
- **RIVA 128ZX:** not finished. Most of 3DMark 2000 is refused and not
  drawn, and one Glide 2 probe check fails.
- **Older, not rerun since:** on the HD 5450, trees vanish and reappear in
  3DMark 2000's Helicopter test (0.33.0); on the Rage 128, 3DMark 2000
  hung once in two runs, and on the Ironlake one early frame draws a piece
  of hillside over the helicopter (0.32.0).

### Recent releases

- **0.45.0:** leaving a full-screen DOS box brings the desktop back on
  every card (GMA 950, HD 4600 and ViRGE/DX tested); a program that dies
  holding DirectDraw surfaces no longer crashes the next one.
- **0.44.0:** colour-keyed Direct3D that writes depth is drawn on the
  Intel GMA X3100 to Ironlake (Forsaken).
- **0.43.0:** the `vbe` package write-combines the screen through the
  page attribute table where the BIOS blocks an MTRR; the full mode list
  returns after a Device Manager reinstall.
- **0.42.0:** OpenGL windows of some widths draw again at 16 bpp on the
  AMD Radeon HD 5450 to 7660D; Rage 128 render targets made after
  textures draw; OpenGL remakes a window the engine refuses.
- **0.41.0:** the AMD Radeon R3E (Mullins); write-combined screen writes
  on the Radeon HD 5450, 5570, 5770, 7470 and 7660D.
- **0.40.0:** Intel GMA 900, 950 and 3150 Direct3D 8 games no longer
  lose geometry; **Always off** vsync no longer shows half-cleared frames
  on Intel GMA 900 to Ironlake; the HD 5570 recognised.
- **0.39.0:** the `vbe` package writes to the screen write-combined (5 to
  80 times faster fills and uploads); the RIVA 128ZX's OpenGL, Glide and
  3DCIF draw on its 3D engine; the S3 Trio64V+ installs under its own
  name.
- **0.38.0:** `GL_EXT_compiled_vertex_array` on every card; OpenGL and
  Glide on the Radeon X1200's 3D engine.
- **0.37.0:** the ATI Radeon X1200 IGP (RS690).
- **0.36.0:** the Intel HD Graphics 4000 mobile; the S3 ViRGE/DX's
  Direct3D on real silicon.
- **0.35.0:** the AMD Radeon HD 7660D (Trinity) and the NVIDIA RIVA 128ZX.

Everything before that is in the [changelog](CHANGELOG.md).

> This is an engineering driver. Each card's results come from **one
> physical machine**: two for the Trio64, none for a card marked
> "recognised, untested". Read the [installation guide](docs/INSTALL.md), and keep a
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
"Tested" means run on the physical machine named; a card that is only
"recognised" installs and binds by design and has not been run.

Jump to: [Intel](#intel) · [AMD and ATI](#amd-and-ati) · [S3](#s3) ·
[Matrox](#matrox) · [NVIDIA, SiS and 3dfx](#nvidia-sis-and-3dfx) ·
[Any VESA card](#any-vesa-card). Within each, by GPU generation, then
oldest chip first. To find
a card by its PCI id, search the page for it (for example `8086:2592`).

#### Intel

| Card | Package | 3D engine | Tested on hardware |
|---|---|---|---|
| Intel GMA 900 (915GM, `8086:2592`) | `intel-gma` | Gen3 | Direct3D (including DirectX 8) at 16 and 32 bpp, on a tester's Acer laptop through remote help; a reporter runs Direct3D on a Lenovo |
| Intel GMA 950 (945GSE, `8086:27AE`) | `intel-gma` | Gen3 | Direct3D, OpenGL, Glide 2 and 3, 3DCIF; DirectDraw on the blitter. From 0.40.0, 3DMark 2001 SE with no batch refused and no flicker with vsync on or off; Dragothic's sky draws white. One HP Mini 110 netbook; a reporter runs 3D on a Nexcom NISE 3270 with an external VGA monitor |
| Intel GMA 3100 (G33/G31, `8086:29C2`) | `intel-gma` | Gen3 | Recognised, untested: the GMA 950's path unchanged, built from one survey of a G31 board. Nothing has run on one |
| Intel GMA 3150 (Pineview, `8086:A011`) | `intel-gma` | Gen3 | Direct3D at 16 and 32 bpp, the OpenGL and Glide 2 probes; DirectDraw on the blitter. 3DMark 2000 at 1024x600x16. One tester's Lenovo netbook (Atom N455) through remote help |
| Intel GMA X3100 (GM965, `8086:2A02`) | `intel-gma` | Gen4 | Direct3D at 16 and 32 bpp with 16 and 24-bit Z, OpenGL with two texture units, the Glide 2 probe; DirectDraw on the blitter. 3DMark 2000 and Quake 2. One tester's HP laptop (Core 2 Duo T7250) through remote help. The alpha test with depth writes is refused, so Direct3D does not draw it; from 0.44.0 colour keys with depth writes are drawn in two passes instead (not run on this machine) |
| Intel GMA X4500 (G45, `8086:2E22`), GMA 4500 (Q45, `8086:2E12`) | `intel-gma` | Gen4 (G4x) | The GM965's path with G4x's encodings, built from one reporter's two surveys. That reporter ran Direct3D, OpenGL and Glide on both machines on 0.28.0, and 3DMark 2000 to 1942 (G45) and 1760 (Q45). No diagnostic files; no later release run ([record](docs/decisions/2026-11-04-g4x-g45-q45-field-report.md)) |
| Intel GMA 4500MHD (GM45/GL40, `8086:2A42`) | `intel-gma` | Gen4 (G4x) | The G45's path unchanged, built from one survey of an HP t5740. That reporter's t5740 (GL40) ran 0.35.0: the desktop at 800x600x16 and Direct3D (3DMark 99, with flicker they put down to vsync off), by their account and a diagnostic report ([record](docs/decisions/2026-11-05-gm45-gl40-field-report.md)). From 0.44.0 the same machine is reached through remote help: Direct3D, OpenGL and Glide 2 probes pass, two-unit REPLACE and DECAL fail as on the GMA X3100 and Ironlake, and Forsaken's menu draws ([audit](docs/decisions/2026-11-08-w9qi3dab-gm45-gl40-audit.md), [issue](docs/issues/2026-11-08-gen4-keyed-depth-write-draws-dropped-forsaken.md)) |
| Intel HD Graphics (Ironlake, `8086:0042`) | `intel-gma` | Gen5 | Direct3D at 16 and 32 bpp with 16 and 24-bit Z, the OpenGL and Glide 2 probes; DirectDraw on the blitter and page flips. 3DMark 2000. One tester's Clarkdale H55 desktop (i3-540) through remote help. No game run |
| Intel HD Graphics 4000 (Ivy Bridge, `8086:0162`) | `intel-gma` | Gen7 | Direct3D at 16 and 32 bpp with 16 and 24-bit Z, one texture stage per pass (0.36.0), the OpenGL and Glide 2 probes, a Glide 3 session; DirectDraw on the blitter and page flips. 3DMark 2000. One tester's MSI B75MA-P45 desktop (i5-3570K) through remote help. 3DCIF not run |
| Intel HD Graphics 4000 mobile (Ivy Bridge, `8086:0166`), from 0.36.0 | `intel-gma` | Gen7 | The desktop HD 4000's path, with the laptop panel (LVDS, 1366x768) behind the panel fitter: Direct3D at 16 and 32 bpp with 16 and 24-bit Z, the OpenGL and Glide 2 probes; DirectDraw on the blitter and page flips. 3DMark 2000. One tester's Lenovo laptop (i7-3632QM) through remote help; its NVIDIA GeForce (Optimus) is left alone |
| Intel HD Graphics 4600 (Haswell, `8086:0412`) | `intel-gma` | Gen7.5 | Direct3D with two texture stages, indexed draws, hardware culling and the guard band, DDI 7 by default; OpenGL with two texture units and transform and lighting on the card, the Glide 2 probe; DirectDraw on the blitter. 3DMark 2000, Half-Life (Direct3D and OpenGL), Quake 3. One Dell i5-4570 desktop. 3DCIF is not supported on it in 0.19.0 |

#### AMD and ATI

| Card | Package | 3D engine | Tested on hardware |
|---|---|---|---|
| ATI Mach64 VT2 | `ati` | none | 2D through the BIOS, on an emulator |
| ATI Rage IIC and the Rage II class | `ati` | Rage II, setup on the CPU | Rage IIC AGP: Direct3D, OpenGL, Glide 2 and 3, 3DCIF |
| ATI Rage XL, Rage Pro, Rage Mobility (Rage Pro class) | `ati` | Mach64 | Rage XL PCI: Direct3D (including DirectX 8), OpenGL, Glide 2 and 3, 3DCIF. Rage Mobility-M: Direct3D and OpenGL, on one Gateway laptop |
| ATI Rage 128 Pro (`1002:5046`) | `ati` | Rage 128 (CCE) | Direct3D at 16 and 32 bpp with 16-bit Z, OpenGL, Glide 2 and 3; DirectDraw and page flips on the engine; GDI by the CPU. 3DMark 2000, Half-Life (Direct3D and OpenGL) and Diablo II (Glide). One Rage Fury Pro, 32 MiB AGP, of which 16 MiB is used |
| ATI Radeon X1200 IGP (RS690, `1002:791F`), from 0.37.0 | `ati` | R300-class, by register writes | Direct3D at 16 and 32 bpp with 16-bit Z, one texture stage, specular, fog, colour keys and mipmaps; DirectDraw on the 2D engine and page flips. Rollcage and 3DMark 2000. From 0.38.0 OpenGL and Glide draw on the 3D engine: the OpenGL and Glide 2 probes pass; 3DCIF not run. One tester's Sempron machine through remote help; 16 MiB of the 128 MiB carve-out used; modes from the VBE |
| AMD Radeon HD 5770 (Juniper, `1002:68B8`), from 0.34.0 | `ati` | Evergreen, through AMD's Juniper CP microcode | Direct3D at 16 and 32 bpp with 16-bit Z, one texture stage; OpenGL; DirectDraw on the DMA engine and page flips. 3DMark 2000 and Quake 3 up to 1600x1200. One tester's machine through remote help; modes from the VBE. The Glide 2 probe passes |
| AMD Radeon HD 5570 (Redwood, `1002:68D9`), from 0.40.0 | `ati` | Evergreen, through AMD's Redwood CP microcode | Recognised, untested: the HD 5770's path with Redwood's numbers, from one reporter's survey. Nothing has run on one yet |
| AMD Radeon HD 5450 (Evergreen "Cedar", `1002:68F9`) | `ati` | Evergreen, through AMD's CP microcode | Direct3D at 16 and 32 bpp with 16-bit Z, one texture stage, specular, fog and colour keys; OpenGL; the Glide 2 probe; DirectDraw on the DMA engine and page flips. 3DMark 2000 and Quake 3. One tester's card through remote help, up to 0.33.0; that card is no longer installed, and 0.34.0 has not run on one; modes from the VBE |
| AMD Radeon HD 7470 (Caicos, `1002:6778`), from 0.34.0 | `ati` | Evergreen (BTC), through AMD's Caicos CP microcode | Direct3D at 16 and 32 bpp with 16-bit Z, one texture stage; OpenGL; DirectDraw on the DMA engine and page flips. Quake 3. One tester's machine through remote help; 2 GiB of video memory above 4 GiB in the GPU's address space; modes from the VBE, up to 1400x1050. The Glide 2 probe passes; 3DMark not run |
| AMD Radeon HD 7660D (Trinity, `1002:9901`), from 0.35.0 | `ati` | Cayman (VLIW4), through AMD's Aruba CP microcode | Direct3D at 16 and 32 bpp with 16-bit Z, one texture stage; OpenGL; DirectDraw on the DMA engine and page flips. 3DMark 2000 and Quake 3 at 1280x1024x32. One tester's A10-5700 machine through remote help; modes from the VBE. The Glide 2 probe passes (0.41.0) |
| AMD Radeon R3E (Mullins, GCN 2, `1002:9854`), from 0.41.0 | `ati` | GCN 2, through AMD's CP and SDMA microcode | Direct3D at 16 and 32 bpp, one texture stage, on the R7E's path with its shaders rewritten for GCN 2; DirectDraw on the SDMA engine and page flips; GDI by the CPU. OpenGL and the Glide 2 probe on the engine. 3DMark 2000 (0.42.0); no game yet. One tester's machine (AMD GX-412HC) through remote help; modes from the VBE. |
| AMD Radeon R7E (Carrizo, GCN 3, `1002:9874`) | `ati` | GCN 3, through AMD's CP and SDMA microcode | Direct3D at 16 and 32 bpp with 16-bit Z, one texture stage, specular, fog and colour keys; OpenGL; DirectDraw on the SDMA engine and page flips. 3DMark 2000 and Quake 3. One tester's HP machine (AMD GX-420GI) through remote help; modes from the VBE, 16 and 32 bpp only. Glide not run |

#### S3

| Card | Package | 3D engine | Tested on hardware |
|---|---|---|---|
| S3 Trio32/64 | `s3` | none | GDI and DirectDraw on the 8514/A engine, on PCI and on a VLB 486 under Windows 95 |
| S3 ViRGE/DX, Trio3D/2X, Trio3D | `s3` | S3D | Trio3D/2X and Trio3D: Direct3D (a narrow path). ViRGE/DX: GDI on the engine; Direct3D and OpenGL tested on an emulator and, from 0.36.0, on one physical card (3DMark 2000 at 800x600x16) |

#### Matrox

| Card | Package | 3D engine | Tested on hardware |
|---|---|---|---|
| Matrox Millennium (MGA-2064W) | `matrox` | none | GDI (fills, copies, text), DirectDraw and page flips on the engine |
| Matrox Mystique (MGA-1064SG, `102B:051A`) | `matrox` | MGA, 16-bit depth only | Direct3D, OpenGL and Glide as on the Millennium II; GDI, DirectDraw and page flips on the engine. One revision 02 card, 4 MiB. The Mystique 220 shares the id and has not run it |
| Matrox Millennium II (MGA-2164W) | `matrox` | MGA | Direct3D, OpenGL and Glide (the engine where exact, the CPU otherwise); GDI and DirectDraw on the engine |

#### NVIDIA, SiS and 3dfx

| Card | Package | 3D engine | Tested on hardware |
|---|---|---|---|
| NVIDIA RIVA 128ZX (NV3, `12D2:0018`), from 0.35.0 | `nvidia` | NV3, 16 bpp (5:5:5) only | Direct3D with 16-bit Z, one texture stage, mipmaps, colour keys, specular and fog; DirectDraw and GDI on the 2D engine at 8 and 32 bpp, page flips. Half-Life, Unreal Tournament, Thief. **Not finished:** 3DMark 2000 runs to the end, but almost all of its draws are refused and not drawn (unplaced textures, refused render targets; from 0.39.0 a texture with no room is refused at creation instead, which completed 3DMark 99 with every draw drawn, and 3DMark 2000 has not been rerun). One tester's STB card on Windows 98 first edition through remote help; modes from the VBE. OpenGL, Glide and 3DCIF draw on the 3D engine from 0.39.0: the OpenGL probe passes, the Glide 2 probe has one failure; no OpenGL or Glide game, and no 3DCIF program, run |
| SiS 6326 | `sis` | SiS 3D | Direct3D, OpenGL; DirectDraw on the engine |
| 3dfx Voodoo4 4500 (VSA-100) | `3dfx` | VSA-100 | Direct3D, OpenGL, and 3dfx's own Glide; GDI on the engine |

#### Any VESA card

| Card | Package | 3D engine | Tested on hardware |
|---|---|---|---|
| Anything with a VESA 2.0 BIOS | `vbe` | none: software rasterizer | Tested on a GMA 950 and a Trio3D, installed through Have Disk. Writes the screen write-combined from 0.39.0 (Radeon R3E, Radeon HD 8570, Rage 128 Pro), through the page attribute table from 0.43.0 when the BIOS blocks that (HD 4600). From 0.46.0 GDI draws into a copy of the desktop in system memory, so screen reads (window moves, scrolling) no longer cross the bus (Radeon R3E, HD 4600) |

The driver also installs on some close relatives of the cards above,
which nobody has run it on yet. Each one uses the code of a tested card
built on the same chip design:

- the other S3 Trio models;
- the rest of the Rage II and Rage Pro lines;
- the Voodoo5, using one of its two chips;
- the Intel HD Graphics 4400 (`8086:041E`), using the HD 4600's code.

These should start up, but are not supported. Their PCI ids are in each
family's manifest under [packaging/families/](packaging/families/), and
the reason for each is in
[docs/specifications/family-manifest.md](docs/specifications/family-manifest.md).

Modes come from each card's video BIOS, merged with the driver's own where
it sets modes itself: up to 1024x768 at 8 and 16 bpp on most cards, True
Color and 1280x1024 on the S3 cards, 1024x576 on the GMA 950's panel, and 1024x600 on the GMA 3150's.
Each package's `INSTALL.TXT` lists them. GDI is drawn by the CPU except
where the table says otherwise. No card has a hardware cursor.

### What each card runs

The interfaces each card has been tested running on physical hardware,
from the records the [changelog](CHANGELOG.md) links.

- **Yes:** tested.
- **Emulator:** tested only on an emulator.
- **Not run:** installed and expected to work, but nobody has tested
  it.
- **No:** not available on that card.

| Card | 2D on the engine | Direct3D | Direct3D 8 | OpenGL | Two texture units | Glide 2 and 3 | 3DCIF |
|---|---|---|---|---|---|---|---|
| Intel GMA 900 | Not run | Yes | Yes | Yes | Not run | Glide 2 probe only | Not run |
| Intel GMA 950 | DirectDraw | Yes | Yes | Yes | Yes | Yes | Yes |
| Intel GMA 3150 | DirectDraw | Yes | Not run | Yes | Probe | Glide 2 probe only | Not run |
| Intel GMA X3100 | DirectDraw | Yes | Not run | Yes | Yes | Glide 2 probe only | Not run |
| Intel GMA X4500, GMA 4500 (reported, 0.28.0) | Not recorded | Yes | Not run | Yes | Not recorded | Yes | Not run |
| Intel GMA 4500MHD | Not recorded | Yes | Not run | Probe | Probe, MODULATE and BLEND only | Glide 2 probe only | Not run |
| Intel HD Graphics (Ironlake) | DirectDraw, flips | Yes | Not run | Probe | Not run | Glide 2 probe only | Not run |
| Intel HD Graphics 4000 | DirectDraw | Yes | Not run | Yes | Yes | Probe only, no game | Not run |
| Intel HD Graphics 4000 mobile (0.36.0) | DirectDraw, flips | Yes | Not run | Probe | Not run | Glide 2 probe only | Not run |
| Intel HD Graphics 4600 | DirectDraw | Yes | Yes, with pixel shader 1.1 (0.34.0) | Yes | Yes | Probe only, no game | No (0.19.0) |
| ATI Mach64 VT2 | No | No | No | No | No | No | No |
| ATI Rage IIC | No | Yes | Not run | Yes | No | Yes | Yes |
| ATI Rage XL, Rage Pro | No | Yes | Yes | Yes | Yes | Yes | Yes |
| ATI Rage Mobility-M | No | Yes | Not run | Yes | Not run | Not run | Not run |
| ATI Rage 128 Pro | DirectDraw, flips | Yes | Not run | Yes | No | Yes | Not run |
| ATI Radeon X1200 (0.37.0) | DirectDraw, flips | Yes | Not run | Probe (0.38.0) | No | Glide 2 probe only (0.38.0) | Not run |
| AMD Radeon HD 5770 (0.34.0) | DirectDraw, flips | Yes | Not run | Yes | No | Glide 2 probe only | Not run |
| AMD Radeon HD 5570 (0.40.0, recognised, untested) | Not run | Not run | Not run | Not run | No | Not run | Not run |
| AMD Radeon HD 5450 | DirectDraw, flips | Yes | Not run | Yes | No | Glide 2 probe only | Not run |
| AMD Radeon HD 7470 (0.34.0) | DirectDraw, flips | Yes | Not run | Yes | No | Glide 2 probe only | Not run |
| AMD Radeon HD 7660D (0.35.0) | DirectDraw, flips | Yes | Not run | Yes | No | Glide 2 probe only | Not run |
| AMD Radeon R3E (Mullins, 0.41.0) | DirectDraw, flips | Yes | Not run | Probe | No | Glide 2 probe only | Not run |
| AMD Radeon R7E (Carrizo) | DirectDraw, flips | Yes | Not run | Yes | No | Not run | Not run |
| S3 Trio32/64 | GDI, DirectDraw | No | No | No | No | No | No |
| S3 ViRGE/DX | GDI | Yes (0.36.0) | Not run | Probe | No | Probe fails | Not run |
| S3 Trio3D/2X, Trio3D | No | Yes, narrow | Not run | Not run | No | Not run | Not run |
| Matrox Millennium | GDI, DirectDraw, flips | No | No | No | No | No | No |
| Matrox Mystique | GDI, DirectDraw, flips | Yes | Not run | Yes | No | Probes | Not run |
| Matrox Millennium II | GDI, DirectDraw | Yes | Not run | Yes | No | Probes | Not run |
| NVIDIA RIVA 128ZX (0.35.0) | DirectDraw, GDI, flips (8 and 32 bpp) | Yes | Not run | Probe only | No | Glide 2 probe only | Not run |
| SiS 6326 | DirectDraw | Yes | Not run | Yes | No | Not run | Not run |
| 3dfx Voodoo4 4500 | GDI | Yes | Not run | Yes | Not run | 3dfx's own | Not run |
| VESA 2.0 BIOS (`vbe`) | No | CPU rasterizer, not timed | Not run | Not run | No | Not run | Not run |

Limits found in testing:
- **Colour depth:** Direct3D renders at 32 bpp, with a 24- or 32-bit
  depth buffer as well as the 16-bit one, on the GMA 950/900/3150, the HD
  Graphics 4600 and the Millennium II (the Mystique, the HD 5450, HD 7470,
  HD 5770, HD 7660D, the R3E, the R7E and the X1200: 32 bpp, 16-bit depth only). OpenGL follows Direct3D's colour depths, with a 24-bit depth
  format (32-bit on the Millennium II) beside the 16-bit one. Every other
  card's 3D, and Glide and 3DCIF everywhere, is 16-bit colour with a
  16-bit depth buffer.
- **Two texture units** means one-pass multitexture: OpenGL's
  `GL_ARB_multitexture` and `GL_SGIS_multitexture`, and lightmaps in one
  pass.
- **Write-combining:** the CPU writes video memory write-combined on the
  Rage 128 Pro, the GMA 950/900/3150 and the Ironlake, on the HD 4600
  through the page attribute table (from 0.33.0, its BIOS leaving no
  memory-type range free), on the SiS 6326 (0.34.0), and on any card on
  the `vbe` package (0.39.0; through the page attribute table when the
  BIOS's ranges refuse, 0.43.0); `C:\V9XDIAG\NOWC.TXT` turns it off on
  these two. `V9X3D OFF` turns it off on the others.
- **When an engine can't draw something:** it refuses the draw rather
  than drawing it wrong. The GMA 950, the two Matrox 3D chips and the
  Rage 128 Pro then draw it on the CPU.

### Tested frame rates

Recorded on each card as it shipped, at 640x480. They are not a
like-for-like comparison: the machines differ, and so do the versions.

| Card | Half-Life `mwd5`, Direct3D | Quake 2, OpenGL | Version |
|---|---|---|---|
| Intel HD Graphics 4600 (i5-4570) | 137.6 fps (windowed) | not run; Half-Life `-gl` 174.4 fps (windowed); Quake 3 `demo001` 56.9 fps | 0.36.0 |
| AMD Radeon HD 5450 | not run | not run; Quake 3 `demo001` 63.6 fps | 0.32.0 |
| AMD Radeon R7E (Carrizo, GX-420GI) | not run | not run; Quake 3 `demo001` 21.3 fps | 0.33.0 |
| AMD Radeon HD 7470 (A10-5700) | not run | not run; Quake 3 `demo001` 34.6 fps | 0.34.0 build |
| AMD Radeon HD 5770 (Core 2 Duo E7500) | not run | not run; Quake 3 `demo001` 49.3 fps at 1024x768x32, 50.6 at 1280x1024x32 | 0.34.0 build |
| AMD Radeon HD 7660D (A10-5700) | not run | not run; Quake 3 `demo001` 33.4 fps at 1280x1024x32 | 0.35.0 build |
| Intel GMA 950 (Atom N280) | 42.4 fps | 19.6 fps (demo2) | 0.9.2 |
| ATI Rage 128 Pro (P3) | 34.4 fps (ATI's driver 59.2) | not run; Half-Life OpenGL 14.1 fps (ATI's 70.1) | 0.28.0 |
| 3dfx Voodoo4 4500 (P3) | 25.7 fps | 20.7 fps (`timerefresh`) | 0.17.0 |
| ATI Rage XL PCI (P3) | 18.1 fps | 11.6 fps | 0.10.1 |
| ATI Rage Mobility-M (P3 laptop) | 12.0 fps | about 8 fps | 0.9.2 |
| SiS 6326 (P3) | 8.6 fps | 7.8 fps (`timerefresh`) | 0.11.0 |
| Matrox Millennium II (P3) | 8.49 fps | | 0.16.0 |
| Matrox Mystique (P3) | 9.06 fps | not run; Half-Life OpenGL 6.03 fps | 0.20.0 |
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

Each engine draws only what testing showed it draws correctly. A draw an engine
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
  the older interface for everything else. On the Rage 128 Pro and the HD
  4600, whose two texture stages are offered through DDI 6, every program
  gets DDI 6.
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

What works so far, tested on the Rage XL and the GMA 950: DxDiag's
Direct3D 8 test, and 3DMark 2001 SE to a full score (111 and 244 3DMarks
at 640x480; its game tests are slow on cards of this age). Direct3D 8 is
offered 16-bit colour only: 16-bit display modes, 16-bit textures and a
16-bit depth buffer, with no stencil. A game that insists on 32-bit
colour, compressed textures or stencil will not start or will drop the
feature. Direct3D 9 needs a newer interface still and is not available.

On the HD 4600, from 0.34.0, DirectX 8 and 9 programs
get the DirectX 8 interface itself (DDI 8) with **pixel shader 1.1**,
cube textures, the signed bump formats and DXT1, DXT3 and DXT5
textures. 3DMark 2001 SE then runs every test, Nature and both pixel
shader tests included, to a full score. A shader beyond 1.1, or one the
driver cannot translate, fails to create rather than draw wrong. Known
issue: Nature's distant terrain and mountains draw white. DirectX 7
programs keep the older interface; `[Velocity9x.Direct3DDdi]
<program>=7` sets one program back if it breaks
([record](docs/decisions/2026-10-30-haswell-ddi8-ps11-record.md)).

## OpenGL

`V9XGL.DLL` is an **OpenGL 1.1 installable client driver**. Windows' own
OPENGL32.DLL finds it through the display driver and loads it in place of
Microsoft's software renderer, so any OpenGL program uses it without
configuration. The driver package installs and registers it.

It offers double-buffered pixel formats with a 16-bit and a 24-bit depth
buffer on a High Color desktop the card can draw into, 565 or 555, and on
a 32 bpp desktop wherever Direct3D draws 32 bpp. It draws through
the same engines as Direct3D, and on the GMA 950 the CPU draws what the
GPU cannot. Quake 2, Half-Life, Serious Sam: The First Encounter and UT99
run on one card or another.

Implemented: the matrix stacks, immediate mode, vertex arrays, clipping
(the scissor included), texture objects and images with the OpenGL 1.1
environments, fog, attribute stacks, state queries, `glReadPixels`,
front-buffer drawing, and `GL_ARB_multitexture` and
`GL_SGIS_multitexture` on the Rage Pro, Gen3, Gen4, the HD 4600 and the
HD 4000. `GL_EXT_compiled_vertex_array` on every card (0.38.0): the lock
is accepted and queried, and a locked draw is drawn as an unlocked one,
with nothing cached yet. Lighting (`glLight`, `glMaterial`, `glColorMaterial`) on the HD
4600 only, where transform and lighting run on the card; every other card
draws OpenGL unlit. Not yet: display lists, evaluators, and feedback and
selection. The [requirements inventory](docs/plans/opengl-1.1-requirements.md)
tracks every entry point. The driver writes `C:\V9XDIAG\V9XGL.LOG`.

## Glide

`GLIDE2X.DLL` and `GLIDE3X.DLL` give games written for 3dfx Voodoo cards
their Glide path on any card with a 3D engine, drawing through the same
render core. Every package except `3dfx` installs both to SYSTEM, but
never over a newer copy, so a Voodoo add-on card keeps its own. The
Voodoo4's package ships none: the card runs 3dfx's own Glide, and the
driver hands the card to it and takes it back.

Tested:
- **Need for Speed II SE:** races on the GMA 950.
- **Carmageddon II:** its Glide option runs, menus and races, on the
  GMA 950.
- **Diablo II** (shareware demo): to the town on the Rage XL, the
  Rage IIC and the Rage 128 Pro.
- **Rollcage** (demo): menus and races on the Rage XL and the GMA 950.
- **HD 4600, HD 4000 and GMA X3100:** the Glide 2 probe passes every
  check, and a Glide 3 session opens and closes. No game has been run
  on them yet.
- **HD 5450 and the Ironlake:** the Glide 2 probe passes every check. No
  game has been run on them yet.
- **X1200:** from 0.38.0 the Glide 2 probe passes every check (0.37.0
  drew no triangles). No game has been run on it yet.
- **RIVA 128ZX:** from 0.39.0 Glide draws on the 3D engine; the Glide 2
  probe has one failure, a 50 % blend one step dark at 5:5:5. No game has
  been run on it yet.

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
GMA 950, VQ-compressed textures included. The HD 4600 is not supported in
0.19.0: it refuses every 3DCIF draw that uses a depth buffer, so most
demos draw only partly, and Laputa hangs. The HD 4000 takes the same path
and has not been run. A tester has tried Formula 1
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
ViRGE's and the NVIDIA RIVA 128ZX's 3D engines draw only 5:5:5, and ATI's
own driver ran 5:5:5, which titles written for ATI cards assume. So on the
ViRGE, the RIVA 128ZX, and ATI cards with hardware Direct3D, the driver
chooses 5:5:5 by itself. Glide games
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

**Try the `vbe` package.** It needs nothing but a VESA 2.0 BIOS and a
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

**From 0.34.0 the floppy build is not maintained:** the shared HAL
outgrew a 1.44 MB disk, and the packages and the gate no longer build it.
Copy the zip's contents by CD, USB or network instead. The script and its
description below are kept for 0.33.0 and earlier, and may fail on a
newer tree.

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
their picture. Lighting runs on the HD 4600 only; display lists and the
rest of OpenGL 1.1 are not implemented yet.

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
Both. Every card in the [table](#supported-cards) marked as tested ran on
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

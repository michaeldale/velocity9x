# Velocity9x

A replacement display driver for Windows 9x, for 1990s PCI and VESA Local
Bus graphics cards — S3 ViRGE, S3 Trio32/64, ATI Mach64/Rage, and generic
VESA cards it has never been told about — and, since 0.8.0, the Intel
GMA 950. It gives a supported card 256-colour, High Color and — on the
S3 targets — True Color modes up to 1280x1024, a DirectDraw HAL with
vertical-blank waits and hardware page flipping on S3, and a hardware
Direct3D path on ViRGE/DX and Trio3D/2X. 0.8.0 adds a second hardware
Direct3D engine, for the GMA 950, and 0.9.0 an OpenGL 1.1 driver on the
same engines.

It is written from scratch against the Windows 98 DDI, DIB Engine, DirectDraw
HAL and Direct3D HAL contracts, rather than derived from anyone's driver
sources. It began as an S3 driver and grew the ATI and generic VESA paths
later.

**Current version: 0.9.0, the OpenGL release.** Beyond 0.8.1 it adds:
- an OpenGL 1.1 installable client driver, `V9XGL.DLL`, loaded by
  Windows' own OPENGL32 - Quake 2 runs on the GMA 950 at 28 fps with
  hardware textures, Serious Sam runs there, and the ViRGE draws the
  textures its S3D can express;
- a neutral render core shared by Direct3D and OpenGL, a render interface
  exported by the HAL, and a CPU rasterizer that takes any blend, the
  alpha test, perspective-correct mip-mapped textures and fog.

Every driver package installs `V9XGL.DLL` along with the display driver,
and the offline transfer disk is now one floppy per family. The 0.9.0
downloads are not published yet.

**Latest published downloads: [0.8.1](releases/0.8.1/README.md).** An
Intel GMA 950 release. Beyond 0.8.0 it adds:
- 3DMark 99 rendering and scoring on the GMA 950 (717 at 640x480, 643 at
  1024x576), with mipmapping;
- DirectDraw fills and copies on the Gen3 blitter, and the application's
  alpha test - Half-Life runs, see-through fences and all;
- 3D WinBench 98's quality suite completing: culling, texture wrap and four
  more blend modes.

See [current status and roadmap](docs/STATUS.md) for defaults, validation
coverage and open work, and [CHANGELOG.md](CHANGELOG.md) for the history,
including what is still open.

> Physical results include Trio64 on PCI under Windows 98 SE, Trio64 on VLB
> under Windows 95, Intel GMA 950 through generic VBE and, since 0.8.0,
> through its own Gen3 Direct3D engine, and S3 Trio3D/2X and
> ViRGE/DX. Coverage differs by feature and build: the Win95 path omits the
> mini-VDD, text acceleration remains opt-in, and ATI is emulator-only.
> Development and regression testing use [86Box](https://86box.net/) and QEMU.
> Read the [verification matrix](docs/STATUS.md) and
> [installation guide](docs/INSTALL.md), and keep a cold backup before installing.

![The Velocity9x page in Windows 98 Display Properties, showing an S3 ViRGE/DX
at 800x600x16 with the linear aperture mapped and a passing GDI test](docs/images/velocity9x-display-properties.png)

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
| See current verification and next work | [docs/STATUS.md](docs/STATUS.md) |

## What it does

Velocity9x replaces the Windows 98 display driver for a supported card. How much
you get depends on the card: an S3 ViRGE gets the full stack down to Direct3D,
while an unlisted VESA card can get an unaccelerated desktop through its BIOS.
The main features, subject to each target's limits:

- **Display modes** — 640x480, 800x600 and 1024x768 at 256 colours and High
  Color (16-bit), plus 640x400 at 256 colours. On the S3 targets, also True
  Color (32-bit) at those three resolutions and 1280x1024 at 256 colours and
  High Color. Resolution *and* colour-depth changes apply live on the ViRGE,
  without a reboot. The generic VESA family additionally asks the video BIOS
  which modes it really has, every boot, and merges the drivable ones into
  its runtime table — widescreen and True Color modes the baseline list never
  named appear in Display Properties automatically, the panel's EDID picks
  the fallback mode when the configured one is gone, and a card whose BIOS
  list is broken simply keeps the baseline modes.
- **2D output** — through the system DIB Engine, with the framebuffer mapped
  linearly. On both S3 chips, GDI solid fills and screen-to-screen copies
  (window moves and scrolls, including overlapping copies in all eight
  directions) run on the 2D engine, with a DIB Engine fallback and a poison
  latch that turns acceleration off for the session after a detected timeout.
  Text, lines and uploads use software by default. ViRGE monochrome uploads
  are opt-in (`GdiAccelUpload=1`), and so, since 0.7.1, is text on Trio64
  and ViRGE (`GdiAccelText=1`). Physical Trio64 text validation
  remains open, so its default stays off. ATI and generic VESA currently have
  no native acceleration backend.
- **DirectDraw** — a flat 32-bit HAL (`V9XHAL.DLL`) providing video-memory
  surfaces and vertical-blank services through the VGA status port. S3 targets
  also implement CRTC display-start page flipping; the HAL declines primary
  flips on ATI and generic VESA, which lack a native scanout backend. The
  guarded Matrox candidate does not package this HAL.
  Solid colour fills and screen-to-screen BitBLT run
  on the chip's 2D engine where there is a backend for one (both S3 parts) and
  fall back to the CPU where there is not (ATI, generic VESA).
- **Direct3D acceleration** (S3 ViRGE/DX and Trio3D/2X) — a deliberately narrow but real
  hardware path through the S3D engine: textured, Gouraud-shaded,
  perspective-correct triangles with mipmapping, trilinear filtering, alpha
  blending, colour-key transparency, specular highlights, fog and Z testing,
  with depth-buffer clears served by the blitter rather than by the CPU.
  Hardware Direct3D selects a matching 5:5:5 High Color desktop automatically.
  Trio3D/2X uses bilinear filtering in place of the ViRGE's two-pass trilinear
  path and has unresolved blend behavior; see [current status](docs/STATUS.md).
  The perspective correction is new in 0.8.0: until 2026-09-23 the caps
  claimed it and every texture was drawn affine. 0.8.0 also
  clips triangles that cross the screen edge, which 3DMark 99's full-screen
  tests had been losing whole. The S3D has no additive or multiplicative
  blend, and some 3DMark tests draw dimmer or plainer for it
  ([record](docs/decisions/2026-09-23-the-tunnel-texture-has-no-checker.md)).
- **Direct3D acceleration on the Intel GMA 950** (new in 0.8.0, run on one
  machine) — a Gen3 engine submitted through the ring:
  - RGB565, ARGB1555 and ARGB4444 textures with mipmaps, modulate, decal,
    Gouraud and flat shading, texture wrap and alpha test;
  - a 16-bit Z buffer with the application's comparison, and colour and
    alpha blending;
  - hardware page flipping, and DirectDraw fills and copies on the blitter
    (0.8.1).

  3DMark 99, Final Reality and Half-Life run on one netbook; see the
  [changelog](CHANGELOG.md) for what is open.
- **OpenGL 1.1** (new in 0.9.0) — an installable client driver,
  `V9XGL.DLL`, that OPENGL32 finds through the display driver and loads in
  place of Microsoft's software renderer on a 16 bpp desktop the engine can
  draw into (565 or 555; 555 only on the ViRGE). It draws through the same
  engines as Direct3D:
  - on the GMA 950, textures of any power-of-two shape sampled by the GPU,
    and anything the GPU cannot express drawn by the CPU into the same
    frame - Quake 2 at 28 fps, Serious Sam drawn with its textures on the GPU;
  - on the CPU rasterizer, every fragment operation the drivers implement;
  - on the ViRGE, square textures in the S3D's own formats, and nothing it
    cannot draw exactly - Quake 3's text and additive effects are refused.

  Immediate mode, vertex arrays, texture objects, state queries,
  glReadPixels and front-buffer drawing are implemented; lighting,
  display lists and the rest of OpenGL 1.1 are not yet
  ([requirements](docs/plans/opengl-1.1-requirements.md)). The INF installs
  and registers it with the display driver.
- **A Direct3D mode selector** on the Velocity9x page in Display Properties,
  offering the chip's own engine, the CPU rasterizer, or nothing at all.
  Turning it off makes the driver advertise no Direct3D at all, so DirectDraw
  enumerates no hardware device and applications fall back to Microsoft's
  software rasterizers — useful on a machine with a second graphics card, or
  when a game misbehaves through the narrow S3D path. It takes effect after a
  restart, and DirectDraw itself is unaffected either way.
- **A Velocity9x page inside Display Properties** reporting the detected
  adapter, PCI ID, installed video memory, active mode and clock, which
  acceleration paths are live, and the driver's own runtime diagnostics.

## Supported cards

Cards are grouped into *families*, one built package each. A family's driver
binary serves every chip in it and picks the right one by PCI id at boot.

| | **S3 ViRGE/DX** | **S3 Trio32/64** | **Intel GMA 950** | **ATI Mach64 / Rage** | **Generic VESA** |
|---|---|---|---|---|---|
| PCI ID | `5333:8A01`, plus `8A13` (Trio3D/2X) | `5333:8811`, plus `8810`, `8812`, `8813`, `8814`, `8901` | `8086:27AE` (945GSE) exactly | `1002:5654`, `1002:4C4D` | `1234:1111`, or anything via Have-Disk |
| Package | `build/win98se-s3` | `build/win98se-s3` | `build/win98se-intel-gma` | `build/win98se-ati` | `build/win98se-vbe` |
| Status | Primary target | Conservative baseline, verified on 2 physical machines | Hardware Direct3D, verified on one physical netbook | Tier-0 bring-up | Tier-0 fallback, verified on a physical Intel GMA 950 and an S3 Trio3D |
| Display modes | 640x400x8; 640/800/1024 at 8, 16 and 32 bpp; 1280x1024 at 8 and 16 bpp | same, subject to BIOS and VRAM | 640x480 and native 1024x576 at 8 and 16 bpp, set by the video BIOS | 640x400x8, 640/800/1024 at 8 and 16 bpp; see Mach64 caveat below | baseline as ATI, plus validated modes from the BIOS |
| Live resolution change | Yes | Yes | Yes, as games switch 1024x576 to 640x480 | Yes | Yes |
| Live colour-depth change | Yes | Yes | Not recorded | Yes | Yes |
| DirectDraw surfaces / vblank | Yes | Yes | Yes | Yes | Yes |
| Hardware primary page flip | Yes | Yes | Yes (through the ring) | No; HAL declines | No; HAL declines |
| Hardware colour fill | Yes (S3D) | Yes (8514/A) | DirectDraw only (Gen3 blitter) | **No** — CPU | **No** — CPU |
| Hardware BitBLT | Yes (S3D) | Yes (8514/A) | DirectDraw only, non-overlapping (Gen3 blitter) | **No** — CPU | **No** — CPU |
| Direct3D | Yes (narrow S3D path) | Software rasterizer, opt-in | Yes (Gen3 3D engine) | same as Trio | Software rasterizer, **on by default** at 16 bpp |
| Direct3D mode selector | Hardware / Software / Disabled | Software / Disabled | Hardware / Software / Disabled | same as Trio | same as Trio |
| GDI acceleration by default | Solid fill + screen copy (S3D) | Solid fill + screen copy (8514/A) | Software; the blitter is not yet used for GDI | Software; no native backend yet | Software; generic BIOS path |
| Hardware cursor | No (software cursor) | No | No | No | No |

The Trio32/64 target accelerates GDI fills and screen copies, plus DirectDraw
fills and blits, at supported depths. Other GDI drawing uses the DIB Engine
by default; Direct3D uses the opt-in software rasterizer.

The generic VESA package is the one that does not wait to be asked: it has no
3D backend on any card, so "hardware Direct3D" is not a thing it can fall back
to, and it ships with the software rasterizer already selected. That only takes
effect at 16 bpp, which is not its default mode, and it is one settings-page
entry away from off.
The ViRGE-only new-MMIO window, S3D engine and hardware Direct3D are not exposed on
it. Its bring-up and boundaries are recorded in
[docs/decisions/2026-08-14-trio64-bringup.md](docs/decisions/2026-08-14-trio64-bringup.md).

The five ids after `8811` are **aliases**: parts the Trio64's code drives
unchanged, bound so they install, but validated nowhere. Only `8901`
(Trio64V2/DX) was even confirmed as an id here, off an option ROM. Treat them
as "it should come up", not as supported. The distinction is enforced rather
than described — an alias cannot carry a VM target and is not covered by the
mode matrix; see
[docs/specifications/family-manifest.md](docs/specifications/family-manifest.md)
and [docs/decisions/2026-08-29-s3-device-id-survey.md](docs/decisions/2026-08-29-s3-device-id-survey.md).

The Trio32 86C732 and the Trio64V+ 86C765 are **not** in that list, because
they publish `8811` itself: the shipping driver has always bound them. That is
now measured both ways — off the option ROMs, and on an 86Box Trio32 guest that
enables, reads its 2 MiB from CR36 and passes eight of the nine declared modes
that fit ([docs/decisions/2026-08-29-s3-trio32-alias-guest.md](docs/decisions/2026-08-29-s3-trio32-alias-guest.md)).
The ninth, 800x600x32, is refused by that card's BIOS
([docs/issues/2026-08-29-trio32-lacks-vbe-0115.md](docs/issues/2026-08-29-trio32-lacks-vbe-0115.md)).

### Verified on physical hardware: S3 Trio64 on PCI

0.4.2 was the first release proven on a real card rather than an emulator, and
0.4.3 adds True Color there. The full stack — the driver, its DirectDraw HAL and
its own mini-VDD — runs on a physical **S3 Trio64 (86C764, 2 MB, Windows 98
SE)**: desktop at 1024x768x16, 32-bpp modes at 640x480 and 800x600 with the
larger ones declined for want of memory, video memory sized from the chip,
hardware fills and screen-to-screen blits on the 8514/A engine, CRTC page
flipping and real vertical-blank services, and the Display Properties page
reporting the card correctly.

It is also *faster than S3's own Windows 98 driver* on that card. In Ironfield
RTS at 640x480 fullscreen, against the stock driver on the same machine:

| Presentation path | Velocity9x | Stock S3 |
|---|---|---|
| Direct back buffer | **27 FPS** | 25 FPS |
| Video memory + `BltFast` | **27 FPS** | 23 FPS |
| System RAM | 20 FPS | 22 FPS |
| Windowed | 18 FPS | 18 FPS |

With 0.6.0's GDI acceleration enabled, CrystalMark Retro 2.1.0 on the same
card (800x600x16) shows the desktop-drawing gain over the driver's own
software baseline — and, just as importantly, the controls that did not move:

| CrystalMark 2D (GDI) | Software (0.5.x) | Accelerated (0.6.0) |
|---|---|---|
| Square | 253 | **275** |
| Image | 91 | **98** |
| Circle | 134 | 134 *(unaccelerated path, bit-identical — a control)* |
| Text | 2 | 3 *(quantisation, not a gain — text is still software)* |

CPU scores were bit-identical across the two runs, which is what makes the 2D
movement attributable to the driver. The run, its prediction, and the honest
caveats (one disk control moved by a similar margin, so the exact percentages
should not be quoted as measured speedups) are in
[docs/decisions/2026-08-27-crystalmark-barry-accelerated.md](docs/decisions/2026-08-27-crystalmark-barry-accelerated.md).
The `AdvFuncRestores=5` counter from that session is the ADVFUNC_CNTL guard —
the fix that made acceleration safe on real silicon — earning its place
against a real workload.

Getting there took two bugs that only real silicon exposed: a 4 MiB video-memory
assumption that is wrong on a 2 MB card, and a mini-VDD that allocated a V86
scratch buffer without paragraph alignment and then truncated its address to a
real-mode segment — harmless on every emulated BIOS, a boot-time protection
error on the physical one. Both are written up in
[docs/issues/](docs/issues/).

### Verified on physical hardware: S3 Trio64 on VESA Local Bus, under Windows 95

0.4.4 adds a second physical machine and the project's first non-PCI one: a
**486 with an S3 Trio64 on VESA Local Bus, running Windows 95 4.00.950**. There
is no PCI bus for Windows to enumerate, so the driver is installed by hand from
a model that claims no hardware ID, and it identifies the chip by reading the
S3's own identity registers instead of configuration space. It comes up at
640x480x8 with the linear aperture mapped at `0x7F000000`, sizes its 2 MB from
the chip, and offers only the modes that fit that much memory.

Three things had to be true at once, and each was its own bug: the identity
registers must be read with the extended-register locks open, because the value
they return through a closed lock is plausible and wrong; the INF must offer a
model with no hardware ID at all, because SetupX cannot bind a `PCI\VEN_` model
on a bus it does not enumerate; and the model must name no mini-VDD, because
ours does not load on Win95 and a display devnode whose mini-VDD fails to load
is a device Windows reports as absent — leaving a working driver that Windows
never asks to enable. The investigation, including the wrong turns, is in
[docs/handoffs/2026-08-22-vlb-manual-select-handover.md](docs/handoffs/2026-08-22-vlb-manual-select-handover.md).

### Verified on physical hardware: Intel GMA 950, on the generic VESA package

The third physical machine is the one that tests the project's central claim —
that the chip-agnostic VBE tier can drive silicon nobody wrote a line of code
for. An **HP Mini 110 netbook (Atom N280, Intel 945GSE, GMA 950,
`8086:27AE`)** runs the `VBE\` package, a driver whose device list names only
QEMU's std-vga: the driver enabled on the first attempt, read the panel's EDID
over the VBE path, picked the native **1024x576** widescreen mode the baseline
list never named, published six modes, and ran the full DirectDraw probe to
`Result=COMPLETE` — every blit path `S_OK`, zero engine timeouts, and
`WaitForVerticalBlank` measuring the panel's 60 Hz to three digits. The
machine has no networking, so the whole result was read off the diagnostic
files afterwards — which is exactly what they are for, and what drove 0.6.0's
diagnostics overhaul (one `C:\V9XDIAG\` directory, honest wording for
unclaimed cards, and the real PCI ids recorded even when the family does not
claim them). The findings and what they changed are in
[docs/issues/2026-08-27-netbook-gma950-findings.md](docs/issues/2026-08-27-netbook-gma950-findings.md).

The one open performance item from that run: with no blitter behind it, video
memory on this tier is fast to allocate and slow to read back (Ironfield
staged at 100 FPS from system RAM against ~20 from VRAM), and the heap policy
change that would steer applications away from the trap is designed but
deliberately unshipped until it is measured.

### New in 0.8.0: Intel GMA 950 with hardware Direct3D

0.8.0 adds an `intel-gma` family for the **GMA 950 on the 945GSE**
(`8086:27AE`, `build/win98se-intel-gma`). It is the first release to ship
it, and it has been run on one machine only.

- **Display:** the video BIOS sets the modes, as on the generic VESA tier:
  640x480 and 1024x576 at 8 and 16 bpp.
- **Direct3D and flipping:** the driver owns the Gen3 engine for Direct3D
  and the plane for page flipping.
- **2D:** nothing is claimed. Fills and blits stay on the CPU.

It has run on one machine, the netbook above. There, Final Reality renders
textured with depth testing and flips in hardware. It flickers one frame in
eight or nine, and 3DMark 99 draws nothing into its buffers. Both are open,
and the same flicker appears on the Trio3D/2X
([state of play](docs/issues/2026-09-19-the-flicker-state-of-play.md),
[3DMark plan](docs/plans/intel-3dmark99-missing-textures.md)).

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

The **Matrox Millennium II** family (`102B:051B`) uses a guarded drop-in
package. Its historical mixed pair — Velocity9x's display driver with the
board's stock Matrox mini-VDD — passed physical software-GDI tests at
640x480x16 and 1024x768x16. That evidence does not validate the current release
archive or replacement of the stock mini-VDD; see the
[bring-up boundary](docs/specifications/matrox-millennium2-bringup.md).

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
`C:\V9XDIAG\V9XBOOT.INI` records how far the driver got and a `VbeDetail` key says which
VESA step refused. Send that.

### Helping add native support

Writing a backend for a chip means knowing what is actually on the board, and
a photograph of the silkscreen is not enough. `V9XSURV.EXE` collects it: the
PCI identifiers and full configuration space, the video BIOS, the VBE mode
list, your monitor's EDID and the raw VGA register file.

**Download it from [releases/](releases/README.md)** — the newest version folder
holds `velocity9x-survey-<version>.zip`.
It is a real-mode DOS program — that is the only place one executable can read
all of the above without a driver. Boot to DOS (`Start` → `Shut Down` →
*Restart in MS-DOS mode*), run `V9XSURV`, and send back the
`C:\V9XDIAG\V9XSURV.INI` it writes. A DOS box inside Windows also works; it just sees
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
S3\            S3 ViRGE/DX and the Trio32/64 family     (5333:8A01, 5333:8811 +5)
ATI\           ATI Mach64 VT2 and Rage Mobility-M       (1002:5654, 1002:4C4D)
VBE\           generic VESA 2.0, for anything else
```

Copy the whole tree to a formatted floppy, or to any other medium the machine
can read. Nothing is archived, because Windows 98 has no built-in extractor
and an offline machine may have no unzip tool — the files are usable straight
off the disk. Add `-Zip` if you want an archive for network transfer instead.

Each package carries a `SHA256.TXT`; after copying you can confirm nothing was
corrupted in transit.

## Common questions

**Will it run on Windows 95 or Windows Me?**
Windows 98 SE is the main target. The manual VLB install is also verified on
one physical Trio64 under Windows 95, without the mini-VDD; see the
[VLB result](#verified-on-physical-hardware-s3-trio64-on-vesa-local-bus-under-windows-95).
That does not establish general Win95 compatibility. Windows Me has no
validated configuration.

**Can I get 32-bit colour, or a resolution above 1024x768?**
On the S3 targets, yes: True Color (32-bit) at 640x480, 800x600 and 1024x768,
and 1280x1024 at 256 colours and High Color. On a 2 MB card the largest of
those are refused for want of memory, which is expected rather than a fault —
1024x768 at 32 bpp needs 3 MB.

ATI's baseline stops at 16 bpp. Generic VESA can add True Color and other
resolutions from the BIOS mode list; the exact choices depend on the card and
the modes the driver validates at boot.

The S3 baseline has no packed 24-bpp modes. No S3 BIOS measured has a
packed 24-bpp mode at all — the VESA numbers usually described as 24-bit
(0x112, 0x115, 0x118) all report 32 bpp on these cards.

**Will my Direct3D games work?**
Compatibility is limited and title-specific. Final Reality and 3DMark 99 run
on the S3 hardware path, while Incoming currently refuses its texture formats.
0.8.0 fixes a run of core faults those two benchmarks exposed:
- state changes that were skipped;
- indexed, strip and fan primitives that were declined;
- full-screen triangles that were dropped instead of clipped;
- textures drawn without perspective correction.

On the Intel GMA 950, Final Reality runs textured, and 3DMark 99 does not
yet draw.
The S3, ATI and VBE packages also offer software Direct3D, with their own
capability limits and no recorded period-machine performance baseline. It is
opt-in on S3 and ATI, and already on in the VBE package, where the alternative
is no Direct3D at all. Expect it to be slow; nothing has measured it on a
period machine.
Start with the
[current status and open issues](docs/STATUS.md), and use Disabled if you want
applications to fall back to another Direct3D device or Microsoft's rasterizers.

**Will the desktop feel faster than with the card's retail driver?**
On the S3 chips, fills and window moves/scrolls now run on the 2D engine —
CrystalMark 2D measures the gain over the driver's own software path on a
physical Trio64 — but text and line drawing are software by default, so a retail
driver keeps an edge on text-heavy work. DirectDraw is the other way round on
the Trio64, where measured frame rates beat the stock S3 driver. The numbers
are in [Verified on physical hardware](#verified-on-physical-hardware-s3-trio64-on-pci)
and [How it compares](docs/comparisons.md).

**Can I run this on real hardware, or only in an emulator?**
Both. Physical evidence covers Trio64 PCI and VLB, generic VBE on Intel GMA
950 and Trio3D, hardware Direct3D on Trio3D/2X and, since 0.8.0, on the
GMA 950, and recent ViRGE/DX GDI tests.
Matrox has historical physical software-GDI evidence with its stock mini-VDD;
ATI remains emulator-only. The [verification matrix](docs/STATUS.md) names the
limits. Read [docs/INSTALL.md](docs/INSTALL.md) first and have a recovery path.

**Do I uninstall the existing display driver first?**
No — and do not remove the display adapter in Device Manager either, because
Windows re-detects it and installs Microsoft's in-box driver instead of this
one. Install over the top with **Have Disk**, per step 6 of
[docs/INSTALL.md](docs/INSTALL.md).

**My card is not in the table. Is it hopeless?**
No. Tier-0 needs nothing but a VESA 2.0 BIOS and a linear framebuffer, so try
the `VBE\` package through Have Disk — see
[Have an unsupported card?](#have-an-unsupported-card) for how, and for what to
send back if it refuses.

## Reporting problems

Include the chip and PCI ID, the package build identifier, the display mode in
use, and the contents of `C:\V9XDIAG\V9XBOOT.INI` — its `Stage` key names the furthest
step the driver reached, and on the `VBE\` and `ATI\` packages a `VbeDetail` key
names which VESA step refused. `C:\V9XDIAG\V9XHW.INI` carries the detected adapter,
memory and stride. If DirectDraw or Direct3D is involved, add `C:\V9XDIAG\V9XDD.INI` and
`C:\V9XDIAG\V9XTRACE.INI`. If the machine failed to reach the desktop, a COM1 serial
capture is the most useful single artefact — [docs/INSTALL.md](docs/INSTALL.md)
explains how to set one up.

One thing worth reporting even when the driver claims success: **if the desktop
is visibly wrong — shredded, repeated, wrong colours — say so.** Pixel readback
can pass while the hardware scans out the wrong layout. The mode matrix has a
separate scanout check, but it does not cover every visible fault; include a
screenshot or photograph alongside the diagnostic files.

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

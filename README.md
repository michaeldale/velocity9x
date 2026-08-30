# Velocity9x

A replacement display driver for Windows 9x, for 1990s PCI and VESA Local
Bus graphics cards — S3 ViRGE, S3 Trio32/64, ATI Mach64/Rage, and generic
VESA cards it has never been told about. It gives a supported card
256-colour, High Color and — on the S3 targets — True Color modes up to
1280x1024, a DirectDraw HAL with real page flipping and vertical-blank
waits, and — on the ViRGE — a hardware Direct3D path.

It is written from scratch against the Windows 98 DDI, DIB Engine, DirectDraw
HAL and Direct3D HAL contracts, rather than derived from anyone's driver
sources. It began as an S3 driver and grew the ATI and generic VESA paths
later.

**Version 0.6.5** — see [CHANGELOG.md](CHANGELOG.md).

> **0.6.0 is the release where this stops being an engineering bring-up and
> becomes a working driver.** The full stack — display driver, DirectDraw HAL,
> mini-VDD, GDI acceleration — runs on three physical machines across three
> buses and two chip vendors: an S3 Trio64 on PCI under Windows 98 SE
> (benchmarked faster than the stock S3 driver in DirectDraw and ahead of its
> own software baseline in CrystalMark 2D), the same chip on VESA Local Bus
> under Windows 95, and an Intel GMA 950 netbook driven by the generic VBE
> package on silicon the driver had never been told about. Development and
> regression testing still happen under [86Box](https://86box.net/) and QEMU,
> the ATI target remains emulator-only, and the per-target caveats below are
> real — so still install it on a machine you have backed up cold, and read
> [docs/INSTALL.md](docs/INSTALL.md) first.

![The Velocity9x page in Windows 98 Display Properties, showing an S3 ViRGE/DX
at 800x600x16 with the linear aperture mapped and a passing GDI test](docs/images/velocity9x-display-properties.png)

## Start here

| If you want to | Go to |
|---|---|
| Download a built driver or the survey tool | [releases/](releases/README.md) |
| Find out whether your card is supported | [Supported cards](#supported-cards) |
| Install it on a test VM or machine | [docs/INSTALL.md](docs/INSTALL.md) |
| Try it on a card that is not listed | [Have an unsupported card?](#have-an-unsupported-card) |
| See how it compares to S3's own driver | [How it compares](#how-it-compares-to-the-retail-s3-drivers) |
| Get it onto a machine with no network | [Transfer disk](#transfer-disk) |
| Recover a machine that will not boot | [packaging/win98se/RECOVER.TXT](packaging/win98se/RECOVER.TXT) |
| Build it from source | [docs/BUILDING.md](docs/BUILDING.md) |
| Help add support for your chip | [Helping add native support](#helping-add-native-support) |
| Understand the design | [docs/specifications/win9x-driver-boundaries.md](docs/specifications/win9x-driver-boundaries.md) |
| See what changed | [CHANGELOG.md](CHANGELOG.md) |

## What it does

Velocity9x replaces the Windows 98 display driver for a supported card. How much
you get depends on the card: an S3 ViRGE gets the full stack down to Direct3D,
while an unlisted VESA card gets a working unaccelerated desktop. What every
target gets:

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
  latch that turns acceleration off for the session if the engine ever wedges.
  Text, lines and CPU-to-screen uploads remain software everywhere, and the
  engineless targets (ATI, generic VESA) are all-software by definition.
- **DirectDraw** — a flat 32-bit HAL (`V9XHAL.DLL`) providing video-memory
  surfaces, CRTC display-start page flipping and genuine vertical-blank
  services on every target. Solid colour fills and screen-to-screen BitBLT run
  on the chip's 2D engine where there is a backend for one (both S3 parts) and
  fall back to the CPU where there is not (ATI, generic VESA).
- **Direct3D acceleration** (S3 ViRGE only) — a deliberately narrow but real
  hardware path through the S3D engine: textured, Gouraud-shaded,
  perspective-correct triangles with mipmapping, trilinear filtering, alpha
  blending, specular highlights, fog and Z testing, with depth-buffer clears
  served by the blitter rather than by the CPU.
- **A Direct3D on/off switch** on the Velocity9x page in Display Properties.
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

| | **S3 ViRGE/DX** | **S3 Trio32/64** | **ATI Mach64 / Rage** | **Generic VESA** |
|---|---|---|---|---|
| PCI ID | `5333:8A01` | `5333:8811`, plus `8810`, `8812`, `8813`, `8814`, `8901` | `1002:5654`, `1002:4C4D` | `1234:1111`, or anything via Have-Disk |
| Package | `build/win98se-s3` | `build/win98se-s3` | `build/win98se-ati` | `build/win98se-vbe` |
| Status | Primary target | Conservative baseline, verified on 2 physical machines | Tier-0 bring-up | Tier-0 fallback, verified on a physical Intel GMA 950 |
| Display modes | 640x400x8; 640/800/1024 at 8, 16 and 32 bpp; 1280x1024 at 8 and 16 bpp | same | 640x400x8, 640/800/1024 at 8 and 16 bpp | same as ATI |
| Live resolution change | Yes | Yes | Yes | Yes |
| Live colour-depth change | Yes | Yes | Yes | Yes |
| DirectDraw surfaces / page flip / vblank | Yes | Yes | Yes | Yes |
| Hardware colour fill | Yes (S3D) | Yes (8514/A) | **No** — CPU | **No** — CPU |
| Hardware BitBLT | Yes (S3D) | Yes (8514/A) | **No** — CPU | **No** — CPU |
| Direct3D | Yes (narrow S3D path) | **No** | **No** | **No** |
| Direct3D on/off switch | Yes | Shown, disabled — nothing to switch | same | same |
| GDI acceleration | Solid fill + screen copy (S3D) | Solid fill + screen copy (8514/A) | **No**, and permanently: no 2D engine | same as ATI |
| Hardware cursor | No (software cursor) | No | No | No |

The Trio32/64 target is intentionally a software-GDI plus DirectDraw baseline.
The ViRGE-only new-MMIO window, the S3D engine and Direct3D are not exposed on
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
describing itself. It is only a reading of what is *advertised*, though, and
that is a real distinction: "Z-buffer sorting" lit up in this list for weeks
while the driver accepted a depth buffer and then ignored it. Depth testing is
backed by the hardware as of 2026-08-30.

Running the benchmark rather than reading its capability list, on an emulated
ViRGE/DX with 4 MiB, all four 3D tests at five repeats:

| Test | Raw speed | Reality marks |
|---|---|---|
| 25 pixel | 23.35 Kpolys/s | 0.75 |
| Robots | 11.54 images/s | 2.99 |
| Fill rate | 42.09 Mpixels/s | 9.11 |
| City scene | 15.50 images/s | 3.85 |
| **3D performance** | | **1.96** |

Those are modest numbers and they are meant to be read as "the path is real and
survives a third-party workload", not as a performance claim: millions of
triangles go through the S3D engine with depth testing live and not one FIFO
timeout or engine reset. The 25-pixel figure is *down* from 28.54 Kpolys/s,
which is the cost of depth actually being done.

**Serving `DDBLT_DEPTHFILL` is a trade-off, and the numbers above are the side
of it this driver currently takes.** A controlled A/B — same guest, same
session, the only difference being a HAL built without the depth-fill path —
gives 23.42 / 9.41 / 67.66 / 11.38 and the same `3D performance` of 1.97. So
moving the depth clear off the CPU and onto the blitter buys Robots 22% and
City scene 36%, costs Fill rate 38%, and leaves the composite where it was. It
also does *not* recover any of the 25-pixel drop, which is what implementing it
was expected to do. Why the fill rate falls is not established; see
[docs/decisions/2026-08-30-ddblt-depthfill.md](docs/decisions/2026-08-30-ddblt-depthfill.md).

FR's own `Visual appearance` percentage is not quoted here. It reads the same
value before and after depth testing began working, and the same value again
for FR's built-in ViRGE reference entry, so it appears to score the advertised
capability set rather than the rendered image. See
[docs/specifications/final-reality-101-runbook.md](docs/specifications/final-reality-101-runbook.md).

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

- **Partial GDI acceleration.** The retail drivers accelerate desktop blits,
  fills and line drawing through the same 2D engine Velocity9x used to reserve
  for DirectDraw. Of those, **solid rectangle fills and screen-to-screen copies,
  including overlapping ones in all eight directions, are now accelerated on
  both S3 chips** (builds `gdi-accel-001` through `003`) — the operations behind
  a desktop fill and a window scroll or move. Line drawing, text and
  CPU-to-screen uploads still go through the DIB Engine in software. On ATI, VBE and Matrox every operation declines and always will:
  those chips have no 2D engine. Every accelerated case keeps a DIB Engine
  fallback, a bounded wait, and a session-long poison latch that turns
  acceleration off for good if the engine ever fails to respond - so the desktop
  survives a wedged engine rather than following it down. See
  [docs/decisions/2026-08-26-gdi-accel-000.md](docs/decisions/2026-08-26-gdi-accel-000.md).
- **No hardware cursor.** The retail drivers use the chip's cursor; Velocity9x
  draws a software cursor.
- **Direct3D is a subset.** Against the retail S3 ViRGE driver's Direct3D
  device description, Velocity9x declares `dwTextureCaps` `0x27` versus
  `0x2F`; the difference is colour-key transparency. It also lacks
  `SORTINCREASINGZ` and `SPECULARFLATRGB`, accepts only pre-transformed and
  pre-lit vertices, and does no clipping, backface culling, lines or indexed
  primitives. The S3D triangle engine writes native ZRGB1555 into a surface
  described as RGB565, which is an unresolved mismatch.
- **Depth gradients are exercised but unverified.** Depth comparison and
  depth-write masking are both pixel-verified. The per-pixel depth slope is
  not: the emulator this is tested on doubles a triangle's start depth but not
  its X gradient, so a sloped test there would measure the emulator rather than
  the driver. Final Reality drives the gradients across sloped scenes without
  faulting, which is not the same as computing the right depth.
- **Fewer modes.** No 24-bpp modes anywhere: no S3 BIOS measured offers one —
  the VESA "24-bit" numbers are all 32 bpp on these cards — so there is nothing
  to drive. The ATI and generic-VESA targets have no high-colour modes above
  16 bpp at all yet, and nothing goes above 1280x1024.
- **No hardware acceleration above 16 bpp.** Both S3 blitters decline at 24 and
  32 bpp and the CPU fallback serves those depths, so DirectDraw fills and blits
  are software there. Direct3D is 16-bpp only.
- **DirectDraw low-resolution modes are unreliable.** 640x400 is reachable
  from GDI but not from `SetDisplayMode`, and the 320x200/320x240 ModeX path
  reports success then fails in use. Applications configured for those modes
  can crash. See
  [docs/issues/2026-08-15-doom95-low-resolution-modes.md](docs/issues/2026-08-15-doom95-low-resolution-modes.md).

Real-application results, including where the driver is known to fall short,
are recorded under [docs/issues](docs/issues).

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
Treat them as untested rather than supported. Everything here has been built and
verified against Windows 98, Second Edition is what the packaging targets, and
there is no INF for 95 or Me.

**Can I get 32-bit colour, or a resolution above 1024x768?**
On the S3 targets, yes: True Color (32-bit) at 640x480, 800x600 and 1024x768,
and 1280x1024 at 256 colours and High Color. On a 2 MB card the largest of
those are refused for want of memory, which is expected rather than a fault —
1024x768 at 32 bpp needs 3 MB.

On the ATI and generic-VESA targets, not yet. Those depend on what the card's
BIOS reports, and no dump has been taken for them.

24-bpp is offered nowhere, and that is deliberate. No S3 BIOS measured has a
packed 24-bpp mode at all — the VESA numbers usually described as 24-bit
(0x112, 0x115, 0x118) all report 32 bpp on these cards.

**Will my Direct3D games work?**
Most likely not. The Direct3D path is real hardware acceleration through the
ViRGE's S3D engine, but a narrow slice of the API: pre-transformed and pre-lit
vertices only, no clipping, backface culling, lines or indexed primitives, and
no colour-key transparency. It is enough to satisfy an application that asks
only for what the driver advertises. It is not a general-purpose Direct3D
device, and there is no Direct3D at all on the Trio32/64, ATI or generic VESA
targets.

**Will the desktop feel faster than with the card's retail driver?**
On the S3 chips, fills and window moves/scrolls now run on the 2D engine —
CrystalMark 2D measures the gain over the driver's own software path on a
physical Trio64 — but text and line drawing are still software, so a retail
driver keeps an edge on text-heavy work. DirectDraw is the other way round on
the Trio64, where measured frame rates beat the stock S3 driver. The numbers
are in [Verified on physical hardware](#verified-on-physical-hardware-s3-trio64-on-pci)
and [How it compares](#how-it-compares-to-the-retail-s3-drivers).

**Can I run this on real hardware, or only in an emulator?**
Three physical machines run it today: an S3 Trio64 on PCI (Windows 98 SE), the
same chip on VESA Local Bus (Windows 95), and an Intel GMA 950 netbook on the
generic VESA package. The ATI target is still emulator-only, and the Matrox
Millennium II candidate has never been run on its physical card at all. Real
hardware is welcome and is where the best bugs have been found — just read
[docs/INSTALL.md](docs/INSTALL.md) first and have a recovery path.

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

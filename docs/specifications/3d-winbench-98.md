# 3D WinBench 98 as a Direct3D conformance suite

What Ziff-Davis' 3D WinBench 98 Version 1.0 is, what it needs, and how
to drive it on a Velocity9x target. Written 2026-09-24 before the first
run; the sections marked **unverified** are to be replaced with what the
installed help files and the first run show.

## Why this suite

3DMark99 tells us a run completed and gives a score. On the netbook's
0.8.0 install it completed with many textures missing and many tests not
running correctly
(`docs\decisions\2026-09-24-netbook-3dmark99-completes-with-textures-missing.md`),
and it could not say which Direct3D feature failed. 3D WinBench 98 was
built for exactly that question: in 1997 Direct3D drivers were uneven, so
Ziff-Davis made a **3D Quality suite** that checks individual rendering
features one at a time, and the performance score (3D WinMark 98) is only
meaningful alongside it - the licence requires the quality results to be
published with any score.

For this project the quality suite is the point, not the score.

## Source

`C:\Users\michael\Downloads\Ziff-Davis PC Benchmarks '98 (Disc 1) Winstone 98
& 3D WinBench 98 (Ziff-Davis, Inc.)(1997).iso` (390,612,992 bytes). Also on
the Internet Archive as "Ziff-Davis PC Benchmarks '98". Layout:

| path | what |
|---|---|
| `ZDBench\3DWB98\` | 3D WinBench 98 installer, InstallShield 3 (`setup.exe`, `data.z` 34,204,545 bytes, `setup.iss`, `Readme.rtf`) |
| `ZDBench\ws98\` | Winstone 98 (business application benchmark; not relevant) |
| `DX5\directx\` | DirectX 5 redistributable (`dxsetup.exe`) |

## Requirements, from `Readme.rtf`

- Windows 95 OSR2 or later; does not run on NT 4.0.
- 486 or better, 32 MB RAM recommended (less works, but paging may
  invalidate results).
- **95 MB disk to install, plus 38 MB to run the quality tests.**
- 16-bit colour or better. The requirements line says 800x600; note 10
  says a 2 MB adapter works at **640x480x16 provided it supports 4444 RGBA
  textures** - without 4444 the benchmark fails with "Can't find a texture
  to unload". That is a driver-visible requirement: advertise 4444 or
  expect that error.
- **DirectX 5.0 or later.** Windows 98 SE ships a newer DirectX
  (the netbook's `DDRAW.DLL` is 299,008 bytes); do not run the disc's DX5
  installer over it.

## Installing

`setup.exe -s` is a supported silent install: no dialogs, no progress,
installs to `C:\ZDBENCH`, program group "Ziff-Davis Benchmarks". The disc's
`setup.iss` response file encodes exactly that. The first launch shows a
licence and a registration window that must be completed by hand before
any test runs.

The InstallShield 3 `setup.exe` is a Win16 stub; launch it through the
remote agent's `exec`, never `shell` - on the netbook any DOS VM moves the
display to the VGA plane
(`docs\decisions\2026-09-24-a-dos-vm-switches-the-netbook-to-the-vga-plane.md`).

## Running

From the Readme:

- **Batch mode** is documented in `RUN3DWB.ZDR` in the installed
  `3DWB98` subdirectory. Known problem: the batch keys
  `3D Scene Tests:DirectDraw Hardware`, `Direct3D Primary` and
  `Direct3D Secondary` do not take effect for the triangle tests; set the
  device in the 3D Test Settings tab instead and remove those lines.
- **Esc** interrupts a test (retry / abort / continue). **Ctrl+F** fails
  the running scene test by hand and asks why - the operator's way to
  record "that frame is wrong" against a named test.
- **`/NOSYSCHECK`** if it fails during initialisation (disables its
  system-info detection).
- If it refuses to run over a misidentified configuration, add to
  `C:\WINDOWS\3DWB98.INI`:

      [ZDBOp]
      TreatConfigProblemsAsWarnings=True

- "During this test the interrupts may have been disabled..." means a
  driver held interrupts off too long; the Readme names the graphics
  driver as the likely cause when it appears here. For us that is a
  finding, not noise.
- Disable power saving on notebooks.

The Test Settings dialog's 3D tab chooses the device (Direct3D HAL or the
software emulation), front/back buffer, resolution, colour depth, full
screen or windowed, and **execute buffers or DrawPrimitive** (the Readme's
publication example lists "Direct3D HAL, MMX Emulation, Front Buffer ...,
640 by 480 pixels, 16-bit color, Full Screen, Execute buffers"). Execute
buffers reach a DX5 HAL through its RenderPrimitive callback, one
`D3DOP_TRIANGLE` instruction at a time - V9XTRACE's
`D3dRenderPrimitiveCalls` - and not through `Execute`, which the runtime
did not call in either this suite or 3DMark99 (`D3dExecuteCalls=0`). An
earlier version of this file read that zero as "execute buffers never
reached the HAL"; that was wrong (corrected 2026-09-25). The DrawPrimitive
API setting would exercise the other entry points (`DrawOnePrimitive`,
`DrawPrimitives`, `DrawOneIndexedPrimitive`).

## Output

- Results are saved as `.ZTD` files under `C:\ZDBENCH\RESULTS`, viewable in
  the PC Benchmark Results Viewer. A `Sample 3D.ZTD` with Ziff-Davis' own
  machines is installed for comparison.
- `C:\ZDBENCH\ERRORS.TXT`: every error the benchmark reported.
- `C:\ZDBENCH\SYSINFO.INI`: what it detected about the machine.
- `C:\WINDOWS\3DWB98.INI` and `ZDBUI32.INI`: settings.

`ERRORS.TXT` and the quality-suite results are the files to collect after
every run.

## The quality suite

Primary source: `C:\ZDBENCH\3DWB98\quality.ini` (55,601 bytes), one section
per test. Each section carries `info=` (what is drawn), `ques=` (the
question put to the operator, always "Is the <feature> implementation
substantially correct?"), `watch=` (what to look at), `HotSpotX/Y`, and
descriptions of one or more **good** and **bad** reference images, with
tips pointing at regions of them.

**The verdict is the operator's.** Before each test a preview page shows
the good reference, the bad reference, a hotspot, and a "Current" panel
that is filled with the test's last frame once it has run. That page is
ordinary GDI, so a remote screenshot sees it, but the test itself renders
full screen and on the netbook a screenshot cannot see a flipped frame.
Some good images record accepted driver quirks - e.g. Fog Table Linear
lists "uncorrected" fog from the projected rather than the camera Z as a
pass "with a comment", because it cannot be fixed before DirectX 6.

The 41 tests, in file order:

Fog Vertex Linear, Fog Table Linear, Fog Table Exponential, Specular
Highlights, Color Key Transparency, Alpha Transparency, Source Alpha Pixel
Blending, Add Pixel Blending, Modulate Pixel Blending, Linear, Nearest
Mipmap Linear, Dithering, Perspective Correction, Fog Vertex and Color
Key, Fog Vertex and Alpha, Nearest, Nearest Mipmap Nearest, Linear Mipmap
Nearest, Linear Mipmap Linear, Z-buffer, Wide Z Accuracy, Narrow Z
Accuracy, Alpha Vertices, Cull counterclockwise, Cull clockwise, Cull
none, Modulate texture blending, Decal texture blending, DecalAlpha
texture blending, ModulateAlpha texture blending, Flat Wrap texture
addressing, Clamp texture addressing, Mirror texture addressing, Flat
shading, Gouraud shading, Cylindrical Wrap u, Cylindrical Wrap v, Texture
Swapping, Anti-aliasing, High Triangle Count, Texture Fidelity.

That is close to a list of the D3D render states and caps a DX5 HAL has
to get right, one feature per test, which is what makes it useful here.

From secondary sources, not checked against the program: a feature the
quality suite finds unsupported is turned off in the WinMark scene tests,
and the scene that needs it scores zero (glossaria.net). Contemporary
criticism (Tom's Hardware, "3D Winbench 98 - Only a Misleading Benchmark
or the Best Target for Cheating?") was that WinMark scores did not track
games and could be raised by benchmark-aimed driver changes - a reason to
use the quality suite and not chase the score.

## Suites and settings, as installed

The Run list offers: 3D API, 3D Features, 3D Processing, **3D Quality**
(41 tests), 3D Resolution, 3D Texture Size, 3D Triangle Tests, 3D WinMark,
All Tests (102), User Defined Scene, Selected.

Edit > Test Settings, 3D tab, defaults: DirectDraw adapter "Primary
Display Driver"; primary Direct3D device **Direct3D HAL**; fallback device
"if the primary is not capable" **MMX Emulation** (the only other choice is
RGB Emulation - there is no "none", so a test the HAL is judged incapable
of silently runs in software); page flipping "Front buffer with a result";
640x480, Hi Color (16 bit), full screen; API **Execute buffers**; quality
interaction: preview dialog on, animation off.

**The D3D HAL Problems tab is a readout of the caps our HAL reports**, one
row per capability with a three-state override. It is the cheapest caps
audit available and needs no test run. Screenshots:
`docs\decisions\2026-09-24-netbook-3dwb98-hal-caps-{1,2,3}.png` and
`...-test-settings.png`.

### First reading: 0.8.0 intel-gma on the netbook

Transcribed from the three screenshots, `HAL Reports` column, before any
test ran (build `890c828`, `EngineCaps=0x214`):

| ON | OFF |
|---|---|
| Alpha Transparency | Add Pixel Blending |
| Alpha Vertices | Anti-aliasing |
| Clamp Texture Addressing | Color Key Transparency |
| Cull None | Cull Clockwise |
| Cylindrical Wrap u | Cull Counterclockwise |
| Cylindrical Wrap v | Decal Texture Blending |
| Flat Shading | DecalAlpha Texture Blending |
| Flat Wrap Texture Addressing | Dithering |
| Gouraud Shading | Fog Table Exponential |
| Linear | Fog Table Linear |
| Modulate Texture Blending | Fog Vertex and Alpha |
| ModulateAlpha Texture Blending | Fog Vertex and Color Key |
| Nearest | Fog Vertex Linear |
| Perspective Correction | Linear Mipmap Linear |
| Source Alpha Pixel Blending | Linear Mipmap Nearest |
| Texture Swapping | Mirror Texture Addressing |
| Textures in video memory | Modulate Pixel Blending |
| Z-buffer | Nearest Mipmap Linear |
| | Nearest Mipmap Nearest |
| | Specular Highlights |
| | Textures in AGP Memory |
| | Textures in system memory |

Eighteen on, twenty-two off. Not yet compared with what the driver means
to advertise; the two cull rows reading OFF while Cull None reads ON, and
every mipmap filter OFF, are the first entries to check against the caps
code.

First results, including why every test is refused on an unmodified run
(CCW culling) and the override that gets past it:
`docs\decisions\2026-09-24-3d-winbench-98-quality-on-the-netbook.md`.
Two operating lessons from that run: **answering No writes an override**
(a No on Cull Counterclockwise resets a FORCE ON to DEFAULT, so later tests
are refused - press Cancel, then Ignore, to skip a verdict without
recording one), and the default "Execute buffers" API reaches our HAL
through RenderPrimitive, not Execute (`D3dExecuteCalls=0` with
`D3dRenderPrimitiveCalls` in the tens of thousands).

## Installing on the netbook, as it went (2026-09-24)

- `setup.exe -s` returned `ResultCode=-12` in `setup.log` (dialogs in a
  different order from the response file): on a machine without
  `C:\ZDBENCH`, setup inserts a "Confirm New Directory" prompt that the
  disc's `setup.iss` does not record. The interactive install works;
  a silent install would need a response file recorded with `-r` here.
- First launch: `C:\ZDBENCH\UI32\ZDBUI32.exe /BENCH=3DWB98`. The licence
  must be scrolled to the end before Proceed enables; registration takes a
  name and organisation.
- The configuration check before a run flags, as yellow (warning only),
  the 1024x576 desktop against its 800x600 minimum and the background
  programs (the remote agent among them).
- **Copying the 34 MB `data.z` with `push-tree` while a screenshot poll
  was running hard-locked the netbook** (desktop frozen, not corrupt).
  The same file sent alone with `put`, nothing else talking to the agent,
  arrived intact in under four minutes. The concurrency is the suspect;
  it is not established.

## Sources

- `Readme.rtf` from the disc (primary).
- https://www.glossaria.net/en/3d-game-graphics/3d-winbench-98-quality-test
- https://www.tomshardware.com/reviews/3d-winbench-98,57.html (page
  content not retrievable on 2026-09-24; cited by title only)
- https://archive.org/details/ziffdavispcbenchmarks98

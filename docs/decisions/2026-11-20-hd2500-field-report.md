# HD 2500 (Ivy Bridge GT1) field report: 3DMark 2001 SE completes, 2696

Date: 2026-11-20
Status: **reported by the owner, one screenshot; no driver diagnostics.**
GitHub issue 29. Follows `2026-11-20-ivybridge-gt1-hd2500.md`, which
claimed `8086:0152` in 0.59.0 from the same owner's DOS survey.

## What was reported

The owner's comment, after 0.59.0 went out: "Omg it works thanks a
lot...", with one screenshot
(`docs\probe\hd2500-field-report-2026-11-20\owner-3dmark2001se-2696-hwinfo.png`):

- 3DMark 2001 SE's Overall Score dialog: **2696**, "not run using default
  settings". The main window's settings line reads "Velocity... 1024x7..."
  and "D3D So[ftware T&L]"; the rest is hidden behind the dialog.
- HWiNFO32 8.40 on Windows 98 SE: Core i3-3250 (Ivy Bridge, 3.5 GHz, 2
  cores 4 threads), ASRock B75M R2.0 (BIOS P1.20, 12/12/2012), 4 GB
  DDR3-1333 (one 2 GB module listed), GPU "Intel Ivy Bridge-DT GT1 -
  Integrated Graphics", "Intel HD Graphics 2500". Computer name W9QI46F4.

## What it shows

- The 0152 row binds and the driver runs on it: the platform line names
  Velocity9x, and no release before 0.59.0 claims 0152.
- 3DMark 2001 SE ran to a score. On `intel-gma` Direct3D defaults to the
  engine, so most likely it drew there with GT1's 48 PS and 36 VS
  threads (`v9x_intel_hsw_3d_set_gt1`) and without a hang; that it was
  the engine and not a fallback is not confirmed from the machine's
  files.

## What it does not show

- Which settings (resolution and depth are only partly visible, detail
  level and texture format not at all), so the score is recorded, not
  compared with anything.
- Whether the frames drew right: no frame was sent.
- The release probes, V9XHW.INI, V9XBOOT.INI or a V9XTRACE snapshot: none
  were sent, so the GT1 limits being the ones in use, the RP0 clock and
  write-combining are not confirmed from the machine's own files.
- OpenGL, Glide and 3DMark 2000.

## Report V9X-JVGQM2, 2026-11-23

The owner then sent a V9XTRACE report on 0.59.0 (`6899f1cd`), with
`V9XHW.INI`, `V9XBOOT.INI`, a snapshot and `V9XGL.LOG`. Description:
"minecraft opengl didn't work", "gpu clock is not showing". From the
machine's own files:

- `Direct3D=hardware-gen7`, `Direct3DMode=hardware`: Direct3D is on the
  engine, not a fallback.
- `RenderClock=rp0-granted`, `RpStateCap=00070D15`: RP0 is 15h, 21 x
  50 MHz = 1050 MHz, the HD 2500's maximum, and it was granted.
  `ClockStatus=unavailable` is hardcoded for every Intel chip
  (`intel_hw16.c`), so the clock is set and only its readout is missing.
  Cosmetic; left for 1.1.
- 1440x900x32 through VBE, `PatWrite=ok`, `MtrrWrite=refused`,
  `StolenBytes=10000000` (256 MiB), `ColourLayout=565-auto`.
- Minecraft: display-list stubs, no textured triangle drawn. Recorded in
  `docs/issues/2026-11-15-tux-racer-display-lists-and-texgen-are-stubs.md`.

## Issue 29 attachments of 2026-11-21 and 2026-11-23

Read 2026-11-23, saved in `docs\probe\hd2500-field-report-2026-11-20\`
as `owner-2026-11-21-*` and `owner-2026-11-23-*`. All on 0.59.0
(`6899f1cd`).

- `V9XBOOT.TXT` and `V9XHW.TXT` repeat what V9X-JVGQM2's copies show
  (above). One more line read: the BIOS uses all eight MTRR pairs and
  covers the aperture with an explicit uncached pair (`Mtrr2=e0000000
  e0000800`), so `MtrrWrite=refused` (`r=11`) and the aperture is
  write-combined through the PAT (`PatWrite=ok pages=00004000`), the
  route of `2026-10-28-haswell-pat-write-combining.md`. Nothing new.
- The Advanced tab shows "Write combining (MTRR)" greyed and unticked.
  In 0.59.0 that box was a placeholder, always unticked
  (`settings_propsheet.c` at `6899f1cd`), so it says nothing about this
  machine; 0.60.0 replaced it with the NOWC switch. "Core clock:
  Unavailable" is the Intel readout deferred to 1.1.
- The Minecraft screenshot shows 1.7.2 windowed at 17 fps with textured
  terrain chunks, under a launcher "Setup" window with "Check compatible"
  ticked. 0.59.0's ICD stubs `glGenLists`, `glNewList` and `glCallLists`,
  and V9X-JVGQM2's session on the same build drew no textured triangle;
  1.7.x keeps its terrain in display lists (from knowledge of the game,
  not checked here). So this frame was most likely not drawn by our ICD.
  The desktop's "Minecraft9x" shortcut may be a pack carrying its own
  `opengl32.dll`; not checked. The display-list item stays as recorded.
  What would settle it: which `opengl32.dll` the game loads, or a
  `V9XGL.LOG` of that session.

## Also in the issue

The same owner attached a V9XSURV `/rom` of a GeForce 9500 GT, `10DE:0640`
rev A1 (G96, Tesla like the GeForce 210 but not its chip), on bus 1 of
the same board: BAR0 `F6000000`, BAR1 64-bit prefetchable at `E0000000`,
BAR3 64-bit at `F4000000`, I/O `E000`, subsystem `0000:0000`, VBE "G96
Board - 09770001" reporting 14 MiB. Saved as
`docs\probe\references\asrock-b75m-gt9500-g96-0640-vgasurv-2026-11-20-a5156b16.ini`;
the `nvidia` package does not claim it.

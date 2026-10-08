# Need for Speed II SE draws through 50 Glide calls: a W-buffer, table fog, paletted textures, chroma key and HUD lines

Date: 2026-10-08
Machine: MICHAEL-NETBOOK (GMA 950), Win98 SE, Velocity9x 0.13.0 set,
boots 120-126.
Driver: as installed; the census `glide2x.dll` (build 63ef2cb-dirty,
`src\glide`) in the game directory. Nothing was drawn.
Evidence: [`../probe/netbook-nfs2se-glide-census-2026-10-08/`](../probe/netbook-nfs2se-glide-census-2026-10-08/)
Plan: [glide-2x-wrapper.md](../plans/glide-2x-wrapper.md), Phase 0.

## Why

The plan trims every later phase to what the first game uses. This is
that list, measured rather than assumed.

## The exports

Read from the disc, not the run. `NFS2SEA.EXE` (the 3Dfx build; the
software build is `NFS2SEN.EXE`) imports exactly 50 Glide names. The two
retail `GLIDE2X.DLL`s on the disc (`SETUP\GLIDENR`, Voodoo Graphics;
`SETUP\GLIDERSH`, Voodoo Rush) export the same 130 stdcall names with
the same argument bytes. `src\glide\glide_entrypoints.psd1` is that
list. The census DLL's export table matches the retail one name for
name, and the game loaded it.

## Getting the game to run (none of it Glide)

1. **`install.win` must exist.** The game reads it from its directory.
   Line 1 is the language, line 2 the install mode, and the rest the
   `INSTALL.NFS` directory list with level-3 entries pointing at the CD.
   Line 2 `remote` skips the CD-drive scan: `0x42E310` compares it with
   a string stored as each byte minus 0x0D. The installer's own label
   for that mode is "installed for multi-player spawned games": its main
   menu offers only CONNECT. For a single race the game was installed
   properly instead, Maximum, from the image mounted on the netbook's
   existing DAEMON Tools as D: (`B125-INSTALL.WIN`). The installer copies
   only `NFS2SEN.EXE` when it finds no 3Dfx board, so `NFS2SEA.EXE` was
   copied in from the disc.
2. **The heap is sized from `dwAvailPageFile`.** `0x4978B8` takes
   `GlobalMemoryStatus`'s free page file (`0x4A0320` reads offset 0x14),
   and refuses under 512 KiB. With 2 GiB installed, Windows 98 reported
   `dwTotalPhys` 7F72E000 + `dwTotalPageFile` 008D1000 = 7FFFF000 and
   none of it free (`B120-MEMSTAT.TXT`). `NFS2SEA.EXE` then aborted with
   "INITMEMMAN REQUIRED BEFORE INITGRAPHICS" and `NFS2SEN.EXE` faulted
   on a null pointer at 00493062.
   - A minimum swap file (`MinPagingFileSize=262144`) changed nothing
     (`B121`): the cap is RAM plus page file, not the file.
   - `MaxPhysPage=40000` (1 GiB) in `[386Enh]` gave 981 MiB free (`B122`),
     and both builds start. **The netbook keeps that setting**; the
     previous `SYSTEM.INI` is `C:\WINDOWS\SYSTEM.NB0`.
3. Injected mouse clicks are ignored in the game's menus; the keyboard
   works. From a fresh start: Escape (intro), Down four times to RACE,
   Enter. Escape does not pause a race.

## What the run used

`B126-V9XGLIDE.LOG`, menus then a race on the default settings. The
counts are from the last summary, after 3,516 swaps. The constant names
are Glide 2.4's as this record reads the numbers; Phase 1 checks each
against the Reference Manual before code depends on it.

| Area | Measured | Calls |
|---|---|---|
| Open | `grSstWinOpen(hwnd 0, res 7 = 640x480, refresh 0, ARGB, origin 0 = upper left, 2 colour buffers, 1 aux)` | 1 |
| Board | `grSstQueryHardware` once; the census answer (Voodoo Graphics, 2 MiB frame buffer, 1 TMU of 2 MiB) was accepted | 1 |
| Vertices | x and y carry the snap bias 3 << 18 (786432.0); `ooz` is 0; `oow` 0.002 to 1.0; `sow`/`tow` 0 to 255; the TMU's own `oow` is uninitialised, so the vertex `oow` is the only divisor | - |
| Triangles | `grDrawTriangle`, about 2,300 a frame in the race | 8,101,381 |
| Lines | `grDrawLine`, screen space (`oow` 1), grey, the HUD's outlines; about 425 a frame | 1,493,275 |
| Points, polygons, AA | none | 0 |
| Depth | `grDepthBufferMode(2)`, the W-buffer, throughout; function 3 (LEQUAL) with writes on, and 7 (ALWAYS) with writes off | 5,010 |
| Fog | `grFogMode(2)`, the table; 20 tables from `guFogGenerateExp`, one loaded by `grFogTable` nearly every frame; colour FF000204 | 4,822 tables |
| Combine | colour and alpha the same, two settings only: function 1 (LOCAL) on iterated (Gouraud, untextured) and function 3 (SCALE_OTHER) by LOCAL, other TEXTURE (texture times Gouraud). `grTexCombine` once: decal (LOCAL) | 25,600 each |
| Blending | (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) and (SRC_ALPHA, ONE); alpha factors zero | 803,361 |
| Alpha test | set once at start (function 4, reference 0x10) | 1 |
| Chroma key | switched on and off around some draws; keys 00000000 and 0000FF00 | 15,578 |
| Clip window | full screen, and HUD panes (532,318-640,453; 0,0-150,70; 486,40-636,110) | 20,404 |
| Clears | colour 00000000 or FF000000, depth FFFF | 5,912 |
| Textures | always square (aspect 3), one level (`lod=n..n`), no mipmapping, bilinear, clamp and wrap; 256 down to 2 texels | - |
| Formats | 11 ARGB1555 (most), 5 P_8 with `grTexDownloadTable` palettes (7 distinct), 10 RGB565, 12 ARGB4444 (menu, 256x256) | 29,218 downloads |
| Texture memory | `grTexMinAddress`/`MaxAddress` and `grTexCalcMemRequired`; the game places downloads itself inside the 2 MiB range | 1,829 sizings |
| Frame buffer | `grLfbLock` four times at start, write-only, 565, stride 2048; no reads, no `grLfbWriteRegion` | 4 |
| Gamma | 1.0, then 1.5 | 3 |
| Swaps | `grBufferSwap(1)` | 3,516 |

Not called in this run: `grAADraw*`, `grDrawPoint`, `grLfbReadRegion`,
`grLfbWriteRegion`, `grDepthBiasLevel`, and every gu texture manager call.

## What render interface ABI 4 cannot express

Each item is a design choice to be agreed before it is coded (plan,
"Interface gaps").

- **Chroma key.** No field. In the DLL it can be approximated by
  rewriting keyed texels to alpha 0 at download and using the alpha
  test. Glide compares the combined colour, though, not the texel. An
  exact key needs an ABI change.
- **Table fog on W.** The ABI carries per-vertex fog in specular alpha.
  The DLL can look the table up per vertex by `oow`; that is vertex fog,
  not per-pixel. Separately, the ICD never sends `fog_enable`, and Gen3
  fogs only Direct3D (2026-10-04), so the render-interface path may
  refuse or ignore it. Phase 2 measures which.
- **The W-buffer.** No ABI change: the DLL writes `sz` from `oow`. Its
  precision on Gen3's Z is to be measured on the probe (plan hazard).
- **Lines.** The ABI draws triangle lists. The DLL widens each line
  into two triangles, about 850 a frame for the HUD.
- **Paletted textures.** No ABI change: the DLL expands P_8 through the
  current palette at download, and again when the palette changes
  (seven palettes in the run).
- **Gamma.** No field; not drawn for now. Recorded as open.

Combine, blending, alpha test, depth compare and mask, clears, clip
window (as scissor) and the four 16-bit formats map onto ABI 4 as it
stands.

## Disputed by the evidence

- The plan expected NFS II SE to need `grLfbWriteRegion` for menus or
  movies. It never called it.
- The plan's census design (log a state call when it changes; sample
  one draw in 4096) failed on the first run. The menu made 80 million
  draws and 107,767 swaps in 40 s, because a swap that draws nothing
  returns at once. The rules became whole-frame captures plus distinct
  argument sets, with a 16 ms wait per swap interval.
- A swap file was the first fix proposed for the memory abort. It did
  not move `dwAvailPageFile` (B121).

## Gates

`check-tree.ps1` and `run-checks.ps1` passed on the census build (before
the logging rework). `build-glide.ps1` and `check-tree.ps1` passed after
it.

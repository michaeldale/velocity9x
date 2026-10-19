# Half-Life OpenGL on the MGA-2164W: every draw goes to the CPU

2026-10-18, A8U4I5 (Matrox Millennium II, MGA-2164W), boot 496, driver
`50e7eeba-dirty` (the code of 4a23aea2). Step 6 of
[`../plans/matrox-mga2164w-render-interface.md`](../plans/matrox-mga2164w-render-interface.md),
for its decision 2. Evidence:
[`../probe/a8u4i5-mga2164w-render-interface-2026-10-18/`](../probe/a8u4i5-mga2164w-render-interface-2026-10-18/)
(`V9XSNA7-boot496-before-hl.ini`, `-after-hl.ini`, and the ICD's `paths` and `hwno` lines in `V9XGL-boot496-hl-paths.LOG`).

## What ran

Michael played Half-Life 1.1.1.0 in Direct3D and in OpenGL on the
800x600x16 desktop and reported both working. A scripted
`-gl ... +timedemo mwd5` launch of mine also ran for about ten seconds
before WCLOSE ended it: it had read the previous session's console log as
the result. So there is **no score**. The console log after the sessions
holds only a timedemo of 0 frames.

The counters below are the difference between two V9XTRACE snapshots of
boot 496. One was taken just before my launch, the other after Michael
quit. They cover all three HL.EXE sessions: `d3d-contexts=2`,
`gl-describes=1`.

## The split

OpenGL sent 267,614 render-interface draws (`R3dDrawCalls`), and the
engine refused every one, so the software fallback drew the whole frame.
It drew, as Michael saw.

**Correction (the same day).** The first version of this record counted
133,572 refusals for "CPU texture levels" (`M64AcceptPolicy02`) as textures
the ICD kept on the CPU. They are not. The ICD's own log (`V9XGL.LOG`,
`paths ... hw-refused=133572/6068291`) shows those are its resends: a
batch whose surface texture the engine refused goes again as the CPU copy,
which the engine refuses as CPU levels before the fallback draws it. The
ICD's reasons for not making a surface (`hwno`) stayed in single figures:
it got surfaces for Half-Life's textures.

So each textured batch was counted twice. Taken once, the first attempts
were:

| First attempts | Batches | Share |
|---|---|---|
| Textured, surface texture (`hw-refused`) and untextured (470) | 134,042 | |
| Refused, surface texture the engine cannot read (`09`, TEXTURE) | 80,417 | 60 % |
| Refused, linear or mipmapped filter (`14`) | 42,789 | 32 % |
| Refused, blend (`04`) | 10,836 | 8 % |
| **Drawn on the engine** | **0** | 0 % |

The three refusals add up to the 134,042 first attempts.

Direct3D, which has no fallback:
- 1,089 batches refused for blend (`M64Policy04`);
- 833 triangles whose planes overflowed the setup (`M64Policy13`,
  `MgaSetupRefusedSplit`).

None of these were drawn. No idle timeouts.

## What this says about decision 2

Decision 2 asks whether explicit draws may take the Direct3D path's
approximations: nearest for linear, the stipple for source alpha. The map
checks the blend, then the texture, then the filter.
- The 32 % refused for filter had surface textures the engine can read.
  Nearest for linear would put them on the engine.
- The 8 % refused for blend are refused before their textures are looked
  at.
- The 60 % whose surfaces `v9x_d3d_mga_resolve_texture` will not sample
  come before either question, and they are the bigger lever. Candidates:
  ARGB4444, which the describe offers but the 2164W does not sample; a
  surface DirectDraw placed itself after `v9x_d3d_mga_create_surface`
  declined it, at a row tpitch cannot state; or a shape outside 1 to 256.

Boot 497 counts that reason (`MgaTexRefused*` in V9XTRACE, with the last
refused surface's bit count, alpha mask, size and pitch). The
approximations are not worth deciding until those textures reach the
engine.

## Boot 497: the refused textures are ARGB4444

Michael ran Half-Life OpenGL again on boot 497 (driver `df84d9e4-dirty`,
the code of b62b2a5f, which counts the reason). The snapshot after
(`V9XSNA7-boot497-hl.ini`; HL.EXE the only process, `gl-describes=1`,
builds agree):

| | Count |
|---|---|
| Render-interface draws | 5,112 |
| Refused for the texture's **format** (`MgaTexRefusedFormat`) | 418; no size, pitch, placement or surface refusals |
| Last refused surface | 16 bits, alpha mask `F000`, 128 x 128, pitch 256: **ARGB4444** |
| First attempts refused for filter / blend / texture | 1,638 (64 %) / 554 (22 %) / 384 (15 %) |
| Resends of refused surface textures (`M64AcceptPolicy02`) | 2,536 |
| Drawn on the engine | 0 |

`MgaTexRefusedFormat` (418) exceeds the 384 texture refusals because the
texture is resolved before the blend is checked: 34 of the blend refusals
carried 4444 textures too.

So the texture lever is one format, and it is closed on this chip:
- The describe offers ARGB4444 surfaces to the ICD for every engine.
- The 2164W's TW12, the 4444 code in 86Box's model, was probed on
  2026-10-10 and drew a garbage pattern with a four-pixel period
  ([2026-10-10-mga2164w-textures.md](2026-10-10-mga2164w-textures.md),
  finding 3; `mga_3d.c` refuses it).
- Matrox's own HAL offers no 4444.

A 4444 texture can reach the engine only converted, and 1555 keeps one
bit of its four of alpha: exact only for alpha that is 0 or 15, which the
ICD does not know. Not offering 4444 surfaces on the 2164W would save the
refused attempt, the VRAM surface and the resend, and change no pixel. The proportions differ from
boot 496 because the session was shorter and played differently. On both,
the filter refusals are the larger share once the texture is sampled.

## Boot 498: half on the engine, and the walls go wrong

Boot 498 runs decision 2 (filters drawn as nearest; no 4444 surfaces;
5f8da79b). Michael ran Half-Life OpenGL. **It ran a little faster than
before, with corruption on the walls**, and Direct3D still looked and
ran better. Snapshot `V9XSNA7-boot498-hl.ini`, and the ICD's lines in
`V9XGL-boot498-hl.LOG`; the probes' 141 draws are taken off:

| | Batches |
|---|---|
| Render-interface draws | 3,531 |
| Drawn on the engine (ICD `paths hw=1993`) | 1,993 (56 %) |
| Refused, blend (`M64AcceptPolicy04`) | 598 |
| Refused, CPU levels (4444 textures, now never surfaces, `02`) | 940 |
| Triangles refused by the setup (`MgaSetupRefusedSplit`), not drawn | 70 |

The ICD's first draw of each state names the two passes of a wall:
- **Base texture:** blend off, depth LESSEQUAL with writes on
  (`blend=00050600 depth=01040102`). Drawn on the engine.
- **Lightmap:** DESTCOLOR/SRCCOLOR, the 2x multiply, depth LESSEQUAL with
  writes off (`blend=01090300 depth=01040001`), logged as `refused`.
  Drawn by the CPU over the engine's pixels.

**Hypothesis, not measured pixel by pixel:**
- The CPU's depth for a polygon differs by rounding from the depth the
  engine stored for it, so the lightmap's LESSEQUAL test fails wherever
  the CPU's value is a step farther.
- Those pixels keep the unlit base texture: the walls' corruption.
- The 70 refused triangles leave holes too.

Phase 0.5's mixed-frame probe tests ordering and encodings, not two
rasterizers agreeing on one surface's depth, so it could not catch this.

Direct3D looks right because it keeps every pass on the engine: it
draws the multiply through the half stipple
(2026-10-10-mga2164w-d3d-engine.md).

## Boot 499: blends on the engine; the Multiplayer menu; lost textures

Boot 499 runs explicit blends as Direct3D's (cb283331). Michael ran
Half-Life OpenGL. Evidence: `V9XSNA7-boot499-*.ini`, `V9XGL-boot499.LOG`,
`V9XWND-boot499.INI`.

**"Stuck" was the Multiplayer menu.** The screen froze on a corrupted
Multiplayer page while the game kept drawing. The readings:
- **Frames rendered, none shown.** The ICD logged about 116 swaps every
  10 s. Render-interface draws rose from 29,686 to 130,352 while the
  HAL's blit copies stayed at 84 and flips at 2, and 116 presents cost
  6 ms in all.
- **No errors.** The ICD's present (a DirectDraw Blt to the primary
  through the window's clipper) logged no `present hr=`. DirectDraw was
  clipping it to nothing before the HAL.
- **The window above the game.** V9XWND, given window rectangles,
  foreground and child controls for this, found a visible, titleless
  `#32770` dialog of HL.EXE's over the Half-Life window, at
  0,0-640,480. Its buttons: Quick start, Internet games, Chat rooms, Lan
  game, Customize, Controls, Spectate Games, Done. It is Half-Life 1.1's
  WON Multiplayer menu, a Win32 dialog painted by GDI.
- **Done closed it**, and the game showed again.

The menu's background art was corrupted. GDI paints it, not OpenGL. Not
investigated. One candidate: explicit draws leave MACCESS's nodither bit
set, and the 16-bit side writes MACCESS once per mode.

**In the game, before the menu:** about 11.6 fps (116 swaps in 10 s), and
about 88 % of the render interface's batches on the engine. Michael:
"it looks a lot better".

**After the menu: about 0.6 fps** (6 or 7 swaps in 10 s, draw 9.7 s of
every 10).
- The engine's batch count stopped rising, and the ICD's `hw-refused`
  rose instead.
- `MgaTexRefusedPlace` reached 8,543 by the end of the session: surface
  textures whose `fpVidMem` lies outside VRAM or off 32 bytes. The last
  was a 64 x 64 565 texture at pitch 128.
- Nothing ran short: 104 textures placed, and no make-failed or
  video-memory refusals on 8 MiB.
- Hypothesis: DirectDraw lost the ICD's surfaces when the menu took the
  screen, and nothing remade them. Boot 500 records the refused
  surface's raw `fpVidMem`, local flags and global flags (DDK
  `DDRAWISURF_INVALID`, `DDRAWISURFGBL_MEMFREE`) to settle it.

## Boot 500: the textures were lost; boot 501 remakes them

Michael ran Half-Life OpenGL on boot 500, opened and closed the
Multiplayer menu, and quit (`V9XSNA7-boot500-hl.ini`). The snapshot:
- 706 surface-texture draws were refused for placement.
- The last refused surface had `fpVidMem` 0, local flags `10000000h`
  (DDRAWISURF_INVALID, "surface has been invalidated by mode set") and
  global flags `1` (DDRAWISURFGBL_MEMFREE, "video memory has been
  freed").
- `gl-describes=2`.

**The textures were lost**, not misplaced and not short of memory.
DirectDraw had freed their video memory, and the ICD kept drawing with the
dead surfaces. The interface's generation had not changed, so the ICD's
`STALE` path never ran: it drops every copy and describes again. Each
batch was refused, resent from the CPU and drawn there.

The change, in the commit with this section:
- **The ICD** asks `IsLost` before it uses a copy (`v9x_gl_hwtex_lost`).
  A lost one is released, made again and refilled, and counted
  (`counters hwtex lost=` in V9XGL.LOG).
- **Glide** does the same on its surface cache, on both the fast path and
  the search (`surfaces-lost=` in the census). Its upload already
  restored a lost surface, but only for a new entry.
- Answering `STALE` from the core instead was rejected: Glide retries a
  `STALE` batch with the same surface, so it would have failed four
  times and dropped the batch.

Boot 501 runs it. V9XR3DP and V9XGLP pass at 16 bpp, and V9XGLIDP fails
only `BlendHalf`, the stipple, as at boot 499. Half-Life after the menu
is not yet measured on it.

## 2026-10-19: the remade textures hold; the menu still corrupts

Michael ran Half-Life OpenGL on the boot 501 build again after a reboot
(trace BootId 2026-10-19 15:48), went through the Multiplayer menu,
played Crossfire and quit (`V9XSNA7-2026-10-19-hl.ini`, and
`V9XGL-2026-10-19-hl.log` with the HL.EXE lines). HL.EXE is in the
trace's process list.

- **The lost textures were remade.** `counters hwtex lost=30` appears in
  the interval of the menu and stays at 30. Every `MgaTexRefused*` is 0,
  and `hw-refused=0/0`.
- **The split.** 55,559 draws (`R3dDrawCalls`), and the ICD's paths add
  up to the same: 52,060 batches (962,442 triangles) textured on the
  engine (93.7 %); 2,071 textured from the CPU and 246 non-square from
  the CPU (`M64AcceptPolicy02` 2,317, the CPU levels the map refuses);
  1,182 untextured, and `M64AcceptPolicy04` (blend refusals) is 1,182
  too.
  `MgaSetupRefusedSplit` 2,350.
- **Frame rate**, swaps per 10 s from the log: 88 to 90 in the first
  intervals, 47 to 48 after the menu, with about twice the batches per
  interval. The scenes differ, so the two are not compared.

Michael's captures:
- In game, lit surfaces show the half stipple of the lightmap multiply,
  every other pixel. That is the approximation chosen at boot 499, not a
  new fault.
- The Multiplayer menu is corrupt as at boot 499: its art is smeared
  into horizontal streaks, its text is drawn twice, a few lines apart, and
  some rows are coloured noise. The other cards do not show it.

One hypothesis killed: GDI at 640x480x16. With A8U4I5's desktop set to
640x480x16 (`V9XMSW /set:640x480x16`), Notepad and the desktop draw
correctly. Not yet separated: whether the menu corrupts under Half-Life's
Direct3D renderer on this card too. If it does not, the fault is on the
OpenGL side (the CPU-drawn non-square pictures, or the engine's
state around a CPU draw). If it does, it is common to both.

### The Direct3D comparison, and the fences

Michael then ran Half-Life's Direct3D renderer through the same menu on
the same build: **the menu was clean**, and the red railings in Crossfire
were cut out. Under OpenGL the same railings were solid red panels. So
both faults are on the OpenGL side.

The fences. Their textures are ARGB4444 in the ICD, a format the 2164W
cannot sample (boot 497). So the CPU drew the base pass, alpha test
included, and the engine drew the lightmap pass over it. Measured with
two new V9XGLP checks (`alpha-glp-16b.ini` in the evidence folder, build `8ae2370e-dirty`): a
16x16 map, red, alpha 0 on the left half, `GL_GREATER` 0.666 over green,
then a grey 128 lightmap with ZERO/SRC_COLOR and LEQUAL:

| Pixel | Read | GL's answer |
|---|---|---|
| `AlphaHole`, `AlphaHoleNext` | 00FF00, 00FF00 | 00FF00 |
| `AlphaSolid`, `AlphaSolidNext` | FF0000, FF0000 | FF0000 |
| `AlphaLitHole`, `AlphaLitHoleNext` | 00FF00, **848284** | 008000 |
| `AlphaLitSolid`, `AlphaLitSolidNext` | FF0000, **848284** | 800000 |

So the alpha test is right, on the CPU, and the half stipple writes
lightmap texels into the holes as well as the solid part. That is the
multiply approximation (boot 499), and Direct3D has it too. What
Direct3D has differently is the base pass on the engine, keyed in
1555. This change gives OpenGL the same: an ARGB4444 image whose
level 0 has only alphas 0 and 15 is uploaded as 1555 for an engine
without 4444. That is exact for those two alphas (`v9x_gl_tex_alpha_binary`,
kept per revision; `v9x_gl_tex_4444_to_1555`; host-tested in
`test_gl_texture.c`). Lower levels round their averaged alpha at 8, and
the 2164W samples the top level only.

On A8U4I5 with that build (BootId 2026-10-19 16:13), V9XGLP passes with
the same alpha pixels, and its textured batches drawn from the CPU went
to the engine (`cpu=3/6` before, `cpu=0/0` now, `alpha-glp-after.ini`).
Half-Life with it is not yet run.

## Not measured

- A timedemo score, OpenGL or Direct3D.
- Which texture formats and shapes Half-Life asked for (counted from
  boot 497).

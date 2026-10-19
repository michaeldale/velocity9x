# Millennium II: OpenGL and Glide through the render interface

Date: 2026-10-18

Status: parked 2026-10-18, see "Parked" below (requested by Michael, 2026-10-18: "plan drawing render-interface
draws on the MGA-2164W's own engine where it can be exact, with the
software fallback for the rest"). Step 1 done 2026-10-18
([record](../decisions/2026-10-18-mga2164w-render-interface-registers.md)):
nodither, the clip window and PLNWT all work on 3D trapezoids at 16 and
32 bpp; modulate is up to two levels darker than the software engine's.
Step 2 done the same day
([record](../decisions/2026-10-18-mga2164w-software-fallback.md)):
V9XDDP `/mixed` passes at 16 bpp and the MGA has the software fallback.
V9XR3DP and V9XGLP pass at 16 bpp. Step 3 done the same day
([record](../decisions/2026-10-18-glide-cpu-copy-retry.md)): Glide resends
a refused surface texture as its CPU copy, and V9XGLIDP passes. Step 4
done the same day
([record](../decisions/2026-10-18-mga2164w-explicit-draws-on-the-engine.md)):
explicit draws go on the engine undithered, with the scissor and the write
mask; the probes still pass, and 51 of their 219 draws went to the engine
(filtering and CPU levels send the rest to the CPU). Step 6 measured
Half-Life OpenGL's split the same day
([record](../decisions/2026-10-18-mga2164w-half-life-gl-split.md)): it draws
correctly, but every draw went to the CPU. Counted once per batch, 60 %
had surface textures the engine would not sample, 32 % were refused for
the filter and 8 % for the blend. Boot 497 named the texture refusals: all
of them are ARGB4444, which the describe offers and the 2164W cannot
sample (TW12 drew garbage on 2026-10-10). Decision 2 taken and applied
at boot 498 (filters as nearest, no 4444 surfaces). Half-Life OpenGL there:
56 % of its batches on the engine, slightly faster, and corrupted walls.
The base pass is drawn on the engine and the lightmap multiply on the CPU,
whose depth for the same polygon differs, and its LESSEQUAL test fails
(hypothesis; [record](../decisions/2026-10-18-mga2164w-half-life-gl-split.md)).
Direct3D, all on the engine with the stipple, looks and runs better.
Michael chose to keep them on the engine with Direct3D's blend
approximations (half stipple for the multiply, stipple for source alpha,
black keyed out of textured additive), applied at boot 499. V9XR3DP and
V9XGLP pass, and 130 of the probes' 138 draws went to the engine.
V9XGLIDP's `BlendHalf` now fails, by design: its one pixel reads a
stippled white `FFFF` where a true blend gives `8410`. At boot 499,
Half-Life OpenGL ran at about 11.6 fps with about 88 % on the engine, and
Michael said it "looks a lot better". Two issues showed:
- The WON Multiplayer menu, a GDI dialog, has corrupted background art.
- After the menu, every card texture was lost: about 0.6 fps until the
  game was restarted.

Boot 501 remakes lost textures in the ICD and in Glide (a6da93e6).

## Parked (Michael, 2026-10-18)

Parked before the boot 501 build was run through Half-Life. A8U4I5 is
left on boot 501 (`69abf8bc-dirty`, the code of a6da93e6), desktop
800x600x16. To pick it up:

1. **Half-Life through the menu.** Michael plays Half-Life OpenGL, opens
   and closes the Multiplayer menu, plays on, then quits.
2. **Read three things** on A8U4I5:
   - `C:\V9XTRCN.EXE`, then `C:\V9XDIAG\V9XSNA7.INI`. Check that the
     process list names HL.EXE: if it does not, the game did not reach the
     HAL. Take `R3dDrawCalls`, `M64AcceptPolicy*`, `MgaTexRefused*` and
     `MgaSetupRefusedSplit`.
   - `C:\V9XDIAG\V9XGL.LOG`, the HL.EXE pid's lines: `time` (swaps per
     10 s is the frame rate), `paths` (`hw=` batches on the engine,
     `hw-refused=`) and `counters hwtex lost=`.
   - The census in `C:\V9XDIAG\V9XGLIDE.LOG` for a Glide title
     (`surfaces-lost=`).
3. **Expected:** after the menu, `hwtex lost` counts the remade copies,
   `MgaTexRefusedPlace` stays near 0, and the frame rate keeps to the
   pre-menu figure.

Done 2026-10-19: the remade textures hold (`hwtex lost=30`, no refusal
for placement), recorded in
[2026-10-18-mga2164w-half-life-gl-split.md](../decisions/2026-10-18-mga2164w-half-life-gl-split.md).

Still open after that:
- The Multiplayer menu's corruption, still there on 2026-10-19. The
  nodither candidate is dead: GDI writes MACCESS, the plane mask and the
  clip before every operation (`v9x_gdi_mga_draw`), not once per mode.
  GDI at 640x480x16 alone draws correctly, and under Half-Life's
  Direct3D renderer the menu is clean: an OpenGL-side fault.
- Fences solid under OpenGL: binary-alpha 4444 textures now go to the
  engine as 1555 (2026-10-19). Half-Life with it is owed; then the menu
  again, since some of its pictures may move to the engine too.
- 1,000-odd triangles a session refused by the setup
  (`MgaSetupRefusedSplit`) and not drawn.
- The untextured additive blends still refused to the CPU (most of
  `M64AcceptPolicy04`).
- 32 bpp: the ICD has no 32-bit pixel formats, and V9XR3DP's oracle is
  16-bit.
- A timedemo score. `+timedemo mwd5` scripted from the command line
  needs a clean console log first; my first try read the previous one.

Next: Half-Life OpenGL on the 2026-10-19 build (fences, then the menu).

Target: A8U4I5 (10.0.1.172), Matrox Millennium II (MGA-2164W, 8 MiB), the
`matrox` family with the Direct3D engine of
[matrox-mga2164w-hardware-3d.md](matrox-mga2164w-hardware-3d.md). It
follows the shape of [sis-6326-opengl.md](sis-6326-opengl.md), the last
engine taken from "draws nothing" to OpenGL on the chip.

## Where it stands

Measured on A8U4I5 at boot 493 with build `13812b70-dirty` (the code of
49f2c02c), on an 800x600x16 desktop
([evidence](../probe/a8u4i5-mga2164w-render-interface-2026-10-18/)):

| Probe | Result |
|---|---|
| V9XR3DP | describe, target, depth and clear pass; draws A, B and C, the textured scissor and the masked draw all return 4 (UNSUPPORTED), with nothing submitted |
| V9XGLP | `Result=PASS` and renderer "Velocity9x Millennium II", but the pixel checks read 0: the three polygon offsets, the three fogs, `RectColor3us` and `Vertex2sColor4b` (Haswell passes all of them) |
| V9XGLIDP (Glide 2) | open, clear, flip and front-buffer checks pass; 14 draw checks fail, and the pixels read the clear colour |
| V9XGL3P (Glide 3) | the session opens and closes; it draws nothing, so it tests no drawing |

The cause is the one the 2026-10-10 record already gives
([2026-10-10-mga2164w-d3d-engine.md](../decisions/2026-10-10-mga2164w-d3d-engine.md),
"Not checked"):

1. `v9x_d3d_mga_map_draw` refuses every draw with `explicit_state`
   (`V9X_D3D_MGA_REFUSE_EXPLICIT`), and every render-interface draw is
   explicit (`r3d.h`). The Direct3D path's approximations (stipple for
   alpha, point sampling, black keyed out for additive) are allowed for
   Direct3D and not for an explicit draw.
2. `v9x_r3d_fallback` gives the software engine to Gen3 only, the one
   engine Phase 0.5 measured to agree with the CPU in a shared frame.
   Every refused draw is therefore lost.

Also seen: A8U4I5 came up at 800x600x8 after the card swap. No engine
renders 8 bpp, so the describe offers no target there. OpenGL needs a
16 or 32 bpp desktop; Glide sets its own mode.

## What an explicit draw asks, and what the 2164W can do exactly

"Exactly" here means that every state field is honoured and the pixels
fall within the tolerances V9XR3DP, V9XGLP and V9XGLIDP apply against the
software engine. A row marked *measure* is a hypothesis until step 1
records it.

| Explicit state | 2164W | Plan |
|---|---|---|
| Untextured flat and Gouraud | trapezoids, coverage exact to the sixteenth (`mga_setup.c`, host-tested) | engine |
| Shading at 16 bpp | dithered by default; MACCESS `nodither` <30> gives plain truncation, measured | engine, with nodither |
| Shading at 32 bpp | no dither (V9XD32 T1 `0013579B`) | engine |
| Depth test and write, all eight compares, Z16 | measured (depth record); Z16 holds z x 65535, as the software engine does | engine |
| Z32 | measured, 2026-10-18 | engine; no fallback (the software engine has 16-bit depth only) |
| Scissor (half-open rows and columns) | CXBNDRY, YTOP, YBOT bound trapezoids, all four inclusive, measured | engine: right and bottom less one |
| Write mask | PLNWT masks trapezoid colour writes per bit, measured at 565 and 8888 | engine |
| Surface texture, 565 or 1555, nearest | measured (textures record); perspective within the texel (host test) | engine |
| Texture combine DECAL / REPLACE | measured | engine |
| MODULATE | texel x Gouraud >> 8: up to two levels darker than the software engine, one 565 step on 9-18 % of inputs | engine only if V9XGLP and V9XGLIDP pass with it (step 5); else fallback |
| Linear, mipmap filters | the chip samples nearest only (Matrox's HAL publishes nearest only too) | fallback |
| Blending other than ONE/ZERO | no blender; the stipple is not exact | fallback |
| Alpha test | none; a 1555 texture with opaque vertices is the alpha-bit key | engine for that case; fallback otherwise |
| Fog, specular | none | fallback |
| ARGB4444, CPU texture levels, a second unit | not sampled | fallback |

## Decisions

1. **The software fallback is the default for every refused explicit
   draw (Michael, 2026-10-18).** It needs the Phase 0.5 mixed-frame
   measurement on this chip first (step 2).
2. **Linear and mipmapped filters are drawn as nearest (Michael,
   2026-10-18, "Allow nearest").** Blending stays exact, refused to the
   CPU. Applied the same day: the map no longer refuses an explicit
   draw's filter, and the describe stops offering 4444 surfaces, which
   the chip cannot sample. The probes still pass at 16 bpp, and 126 of
   their 141 draws went to the engine (A8U4I5 boot 498). The original
   text of the decision: **Approximations stay out of explicit draws
   (recommendation below).** Most OpenGL titles use linear filtering and alpha blending,
   so on exact terms Quake 2 and Half-Life's textured world will mostly
   draw in software. Drawing linear as nearest, or source alpha as the
   stipple, would put them on the engine, as Direct3D already does.
   Recommendation: no, so that the probes keep their meaning. Measure the
   split first (step 6), then decide with numbers. No INI key gates
   either choice.

## Steps

1. **Probe, no product code.** Extend the MGA2D probe (MGA2D.EXE pattern)
   with Gouraud trapezoids:
   - MACCESS `nodither` at 16 bpp, compared with the undithered value;
   - a clip rectangle that starts inside the target (CXBNDRY, YTOP,
     YBOT), reading all four edges;
   - PLNWT channel masks at 565 and 8888;
   - TEXCTL tmodulate against the software engine's modulate on the same
     texels.

   Record each result in `docs/decisions/YYYY-MM-DD-mga2164w-<topic>.md`,
   including the cases that fail.
2. **Mixed frame.** `V9XDDP /mixed` on A8U4I5 at 16 and 32 bpp: the
   engine and the CPU in one target and depth buffer, with the drain
   between them. If all three cells pass, add the MGA to
   `v9x_r3d_fallback` (Z16 only; a draw with Z32 bound is refused rather
   than given to an engine that cannot read the buffer). Re-run V9XR3DP,
   V9XGLP and V9XGLIDP: every refused draw should now be drawn by the
   CPU.
3. **Glide's refused hardware texture.** The fallback takes no surface
   texture (`d3d_core.c`). The ICD answers UNSUPPORTED by resending its
   CPU copy; `glide_core.c` does not. Give Glide the same retry with its
   converted CPU level, test-first in the host tests.
4. **Map explicit draws** in `d3d_mga_map.c` on the terms of the table,
   test-first:
   - a refusal per row marked fallback, each with its own reason;
   - the scissor and the write mask as step 1 found them;
   - the shading mode as step 1 found it;
   - flat colour without depth as the flat trapezoid, which is exact at
     any depth.

   `d3d_mga.c` carries the scissor to the clip registers and restores
   them for the 2D engine, as `v9x_mga_build_fill` expects.
5. **Probes on A8U4I5** at 16 and 32 bpp: V9XR3DP, V9XGLP and V9XGLIDP
   all pass. The trace records how many draws the engine took and how
   many the fallback took.
6. **Applications.** Half-Life `-gl` `timedemo mwd5` at 640x480, then a
   Glide title already on the machine (Diablo II or Rollcage, with the
   current `glide3x.dll` in the game folder). For each, record the
   engine/fallback split and the score; scores are recorded, not
   compared. These numbers settle decision 2.

Gates for every step that changes code: `build-host.ps1` (the failing
test first), then `run-checks.ps1`, then the deploy by WININIT rename.

## Not in scope

- The MGA-2064W: it has no texture engine and stays on the software
  engine.
- Approximate explicit draws, unless decision 2 changes.
- 8 bpp OpenGL.
- Two texture units.
- Performance work, which stays parked
  ([matrox-mga2164w-performance-parked.md](matrox-mga2164w-performance-parked.md)).

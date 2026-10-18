# 32 bpp on the engines: 2D acceleration, then Direct3D and the render interface

Date: 2026-10-18. Status: in progress. Phase 1 Gen3 done (netbook boot 208,
[2026-10-18-gen3-blits-at-32-bpp.md](../decisions/2026-10-18-gen3-blits-at-32-bpp.md));
Phase 2 done for the software engine (86Box Trio64,
[2026-10-18-software-direct3d-at-32-bpp.md](../decisions/2026-10-18-software-direct3d-at-32-bpp.md));
decision 1 taken, decision 4 owed. Follows
[high-depth-dynamic-modes.md](high-depth-dynamic-modes.md), which shipped
32 bpp desktops in 0.6.0 on the CPU and deliberately left the engines out
("No hardware acceleration at 24/32 ... D3D stays 16bpp-only").

## Where 32 bpp stands today

Every family lists 32 bpp modes where its BIOS does (the SiS 6326 BIOS has
none; it offers packed 24 bpp,
[2026-10-05-sis6326-first-velocity9x-bind.md](../decisions/2026-10-05-sis6326-first-velocity9x-bind.md)).
What draws in those modes, read from the tree at 663258b4:

| Engine | DirectDraw fill/copy at 32 | GDI at 32 | Evidence or gate |
|---|---|---|---|
| S3 Trio64 (8514/A) | declined | declined | `eng_s3_trio.c:100,164`; never run above 16 |
| S3 ViRGE / Trio3D (S3D 2D) | declined | declined | `eng_s3_virge.c:332,390`; format field codes 8/16/24 only (`:365`) |
| ATI Mach64 / Rage IIC | declined | no GDI path | `eng_mach64.c:212,288` accept 2 bytes only |
| Intel Gen3 (BLT) | **engine** since 2026-10-18 | no GDI path | measured 8/16/32, [2026-10-18-gen3-blits-at-32-bpp.md](../decisions/2026-10-18-gen3-blits-at-32-bpp.md); was declined at 32 |
| Intel Gen7.5 (BCS) | **engine** | no GDI path | measured 8/16/32, [2026-10-17-haswell-phase-b2-directdraw-on-bcs.md](../decisions/2026-10-17-haswell-phase-b2-directdraw-on-bcs.md) |
| Matrox MGA-2064W/2164W | **engine** | **engine** | measured, [2026-10-09-mga2064w-gdi-on-the-engine.md](../decisions/2026-10-09-mga2064w-gdi-on-the-engine.md) |
| 3dfx VSA-100 | builder accepts 4 bytes (`tdfx_2d.c:26`) | gate 4 declines (`gdi_accel.c:1715`) | measured at 16 only |
| SiS 6326 | n/a (no 32 bpp mode) | n/a | — |

Direct3D: every engine's limits name `target_bits_per_pixel = 16` and
`depth_bits_per_pixel = 16` (`d3d_*.c`), `d3d_core.c:1175` classifies only
16-bit target formats, and `d3d_core.c:1281,1508,1560` refuse any target
whose depth differs from the engine's. On a 32 bpp desktop the HAL offers
no device at all; titles fall to Microsoft's RGB emulation (seen on the
ViRGE, [2026-09-19 ViRGE decision](../decisions/2026-09-19-virge-86box-the-flip-chain-rebinds-correctly-and-the-emulator-cannot-answer-the-timing.md)).
The OpenGL ICD, GLIDE2X/GLIDE3X and ATI3DCIF sit on the same core and
inherit the 16 bpp limit. (That was the state at 663258b4. Since
2026-10-18 the software engine renders 32 bpp and the hardware engines
still refuse it; see Phase 2.)

## What each chip's silicon is said to do at 32 bpp

None of the 3D rows below has been measured by this project except the
MGA-2164W's, which was probed at 800x600x32 with 16- and 32-bit Z
([2026-10-10-mga2164w-depth.md](../decisions/2026-10-10-mga2164w-depth.md)).
The rest are what the databooks or the existing builders say; each is a
hypothesis for Phase 0, not a fact.

| Chip | 2D at 32 | 3D colour at 32 | Deeper Z | Source |
|---|---|---|---|---|
| Trio64 | 32 bpp pixel mode exists on Trio64 (Trio32 alias guest passed a 640x480x32 desktop) | no 3D | — | unmeasured on the engine |
| ViRGE / DX / Trio3D | no 32 code in the S3D format field; 24 packed only | S3D destination 8/16/24 | no | `eng_s3_virge.c:365`, S3 DDK reading in [2026-09-02-s3d-writes-1555...](../decisions/2026-09-02-s3d-writes-1555-because-it-can-only-write-1555.md) |
| Mach64 (Rage IIC, Pro/XL, Mobility) | DST_PIX_WIDTH 6 = 32 bpp | 8888 destination claimed | 16-bit only (to confirm) | [2026-08-16-ati-mach64-hardware-audit.md](../decisions/2026-08-16-ati-mach64-hardware-audit.md):477 |
| Intel Gen3 (GMA 900/950) | BR13 depth 3<<24 | ARGB8888 colour buffer | D24 (S8) | [2026-08-17-intel-gma-gen3-hardware-audit.md](../decisions/2026-08-17-intel-gma-gen3-hardware-audit.md):261; [intel-24-bit-depth.md](intel-24-bit-depth.md) |
| Intel Gen7.5 (HD 4600) | measured | B8G8R8A8/X8 | D24 or D32F | Haswell PRMs; unmeasured for 3D |
| MGA-2164W | measured | PWIDTH_32 trapezoids measured by the probe | 32-bit Z measured | `mga_3d.c:559`, depth decision above |
| MGA-2064W | measured | no 3D engine used | — | stays software |
| VSA-100 (Voodoo4) | format 32BPP in the builder | 32 bpp rendering | 24-bit Z + 8 stencil | VSA-100 docs; unmeasured |
| SiS 6326 | — | — | — | out of scope: no 32 bpp mode |

## Decisions

1. **Depth buffer with a 32 bpp target: decided (Michael, 2026-10-18).**
   Offer 24-bit Z (32 on the MGA) wherever the engine has it, alongside
   16; the app chooses. 3D WinBench's Z accuracy tests are the reason to
   want it.
2. **Which DirectDraw format a 32 bpp primary reports (owed).** X8R8G8B8 (alpha
   mask 0) is what the stock drivers of this era report and what the CPU
   path already assumes. Recommendation: keep X8R8G8B8 for the primary,
   accept A8R8G8B8 for offscreen render targets only on engines that write
   destination alpha.
3. **Order of chips (owed).** Recommendation below, by machine availability and how
   much is already measured.
4. **Software fallback at 32 bpp (owed).** On a machine in hardware mode
   whose engine does not draw 32 bpp, should a 32 bpp title get the
   software engine instead of no HAL device (and so Microsoft's RGB
   emulation)? Phase 2 measured the software engine correct at 32 bpp
   but not its speed against Microsoft's. Recommendation: no fallback
   until a title has been timed on both.

No INI key or V9X3D verb gates any of this:
each piece becomes the default in
the build that lands it, per engine, once its gate passes.

## Phase 0 — measure, per chip (no product code)

For each chip in the order of Phase 3, before its gate is lifted:

- 2D: the chip's existing write probe (MGA2D.EXE pattern) at 32 bpp —
  fill, copy both directions, overlap, clip edges, compared against the CPU.
- 3D: one flat and one Gouraud triangle to a 32 bpp target, with 16- and
  (where offered) 24/32-bit Z, read back and compared against the
  software rasterizer at 8888.
- Record each in `docs/decisions/YYYY-MM-DD-<chip>-32bpp.md`, including the
  rows that fail. A chip whose 3D row fails keeps 32 bpp 3D off and says so
  in its family plan.

## Phase 1 — 2D at 32 bpp

Lift each depth gate only where Phase 0 passed; no shared change is needed
beyond `gdi_accel.c` gate 4 becoming a per-engine depth mask instead of the
MGA special case.

1. **VSA-100**: DirectDraw already reaches the builder at 4 bytes; measure,
   then admit 32 in GDI gate 4. Cheapest win.
2. **Mach64 family** (Rage IIC, Pro/XL, Mobility via the shared hook: a fix
   measured on one goes to all): DST_PIX_WIDTH 32 in the fill and copy builders,
   the 32-bit colour path in the 2D mode block; host tests extended to 4
   bytes.
3. **Intel Gen3**: done 2026-10-18. BR13 depth 3, the whole fill DWORD and
   the write bits 21:20; fills and copies pixel-exact on the blitter at
   8, 16 and 32 bpp on the netbook.
4. **Trio64**: probe the 8514/A engine at 32 bpp on the 86Box Trio64 and the
   486 VLB card. Low priority; keep the decline if either disagrees.
5. **ViRGE family**: stays CPU at 32 bpp; no 32-bit S3D format exists. Recorded,
   not planned.

Gates: host tests for every builder change; V9XGDI `/accel` and V9XDDP blit
cases at 32 bpp on the chip's machine.

## Phase 2 — the Direct3D core accepts a 32 bpp target

Done 2026-10-18 for the software engine, with the differences recorded in
its decision: the opt-in is one appended limits field,
`target_pitch_max_xrgb8888`, rather than format masks; the software
depth buffer stays 16-bit; and no hardware-mode fallback (decision 4).
`V9XD32.EXE` is the instrument for every chip's Phase 3 row. As planned:

Shared work, host-testable, done once:

- `V9X_D3D_ENGINE_LIMITS`: replace the single `target_bits_per_pixel` and
  `depth_bits_per_pixel` with masks of target formats (565, 555, X8888,
  A8888) and depth formats (Z16, Z24, Z24S8, Z32). Every engine starts with
  today's masks, so nothing changes until an engine opts in.
- `v9x_d3d_target_format_of` (`d3d_core.c:1175`) classifies 32-bit
  formats; the three equality refusals become mask tests.
- Driver caps: `dwDeviceRenderBitDepth` and `dwDeviceZBufferBitDepth`
  built from the masks of the current mode's engine; the Z-buffer pixel
  formats DDI 6 enumerates likewise.
- Surface placement (`d3d_place.c`) sizes 4-byte targets and Z; a mode
  whose front, back and Z do not fit in VRAM offers no 32 bpp device rather
  than failing at create.
- Clears, colour-key, fog and specular colour packing take the target
  format; the CPU rasterizer (`d3d_raster.c`) gains an 8888 writer and Z24.
- **The software engine opts in first.** It is the reference every chip is
  compared against in Phase 0, and it gives a 32 bpp desktop a HAL device on
  every family, including ViRGE and SiS, before any chip work.

Gates: `build-host.ps1` with new raster tests (8888 pixels, Z24 compare);
V9XDDP core at 32 bpp on the software engine on a QEMU or 86Box guest.

## Phase 3 — per-chip Direct3D at 32 bpp

Each engine sets its masks after its Phase 0 3D row passed, then runs
V9XDDP core and one real title at 32 bpp on its machine.

| Order | Chip | Machine | Notes |
|---|---|---|---|
| 1 | MGA-2164W | A8U4I5 (when re-fitted) / 86Box Millennium :9877 | 32 bpp trapezoids and 32-bit Z already probed |
| 2 | Intel Gen3 | netbook | merges with [intel-24-bit-depth.md](intel-24-bit-depth.md); 8 MiB stolen memory makes 1024x768x32 triple-buffered + Z tight |
| 3 | Intel Gen7.5 | Haswell 4570 | after its Phase G games |
| 4 | VSA-100 | A8U4I5 Voodoo4 | 24-bit Z + stencil |
| 5 | Mach64 3D (Pro/XL, Mobility, IIC) | A8U4I5 Rage XL, Gateway, A8U4I5 IIC | 4 MiB IIC fits 800x600x32 double-buffered with Z16, not 1024x768 |

ViRGE, Trio3D and SiS 6326 get the software engine at 32 bpp and nothing more.

## Phase 4 — the render interface at 32 bpp

- **OpenGL ICD**: a windowed GL app on a 32 bpp desktop is the user-visible
  case. Pixel formats offered from the engine masks (32-bit colour, 24-bit
  depth where present); Quake 2 windowed at 32 bpp on the netbook and the
  Rage XL as the gate.
- **GLIDE2X / GLIDE3X**: Glide titles set their own 16 bpp fullscreen mode;
  no change unless a census finds one that renders into a 32 bpp desktop.
- **ATI3DCIF**: census first; the demos measured so far run 16 bpp.

## Out of scope

Packed 24 bpp acceleration (ViRGE, SiS); 32 bpp textures beyond what each
engine already samples; stencil operations beyond clearing the stencil bits
of a Z24S8 buffer; benchmark comparisons between 16 and 32 bpp.

## Critical files

- `src/display32/engines/eng_{tdfx,mach64,i9xx,s3_trio}.c`,
  `src/chipsets/{tdfx/tdfx_2d.c,ati/mach64_engine.c,intel/i9xx_blt.c}`
- `src/display16/gdi_accel.c` (gate 4)
- `src/display32/d3d/d3d_internal.h`, `d3d_core.c`, `d3d_place.c`,
  `d3d_raster.c`, `d3d_soft.c`, then each `d3d_<engine>.c`
- the ICD pixel-format enumeration
- `tests/host/` raster and builder tests

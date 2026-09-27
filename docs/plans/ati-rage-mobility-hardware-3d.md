# ATI Rage Mobility-M hardware Direct3D and OpenGL

Date: 2026-09-27

Status: proposed — research complete; Phase 0 not started. No Mach64 engine
write is authorised by this document alone.

## Goal

Add hardware Direct3D and OpenGL rendering for the physical ATI Rage
Mobility-M (`PCI 1002:4C4D`, Mach64LM) by implementing one Rage Pro-class
fixed-function backend beneath Velocity9x's shared render interface.

The destination is deliberately narrow:

- 16-bpp RGB565/XRGB1555 rendering;
- one local-VRAM texture unit;
- untextured and textured triangles;
- Z16, alpha test, additive blending, fog where it does not conflict with
  blending, scissor, flat and Gouraud shading;
- truthful Direct3D capabilities;
- the existing `V9XGL.DLL` ICD routing supported draws to the same hardware;
- whole-draw software fallback only after mixed-engine ordering and pixel
  encodings have been measured safe.

This is not a second OpenGL driver. Direct3D and OpenGL already converge on
`V9X_R3D_DRAW`; the new work is one engine implementation and the substrate
under it.

## Starting point

The `ati` family is tier-0 today:

- the VBIOS sets modes through VBE;
- the linear framebuffer is CPU-drawn;
- both ATI chips declare `EngineType = NONE` and no engine capabilities;
- no native 2D, vblank, flip or 3D operation is advertised;
- the physical Mobility-M has 4 MiB SDRAM and a fixed 1024x768 panel;
- 86Box emulates a Mach64 VT2 2D engine, not the Mobility's Rage setup engine.

The physical identity and register-window facts are settled in
[`../decisions/2026-08-16-ati-mach64-hardware-audit.md`](../decisions/2026-08-16-ati-mach64-hardware-audit.md):

- BAR0 LFB `F5000000` on the measured laptop;
- BAR2 MMIO `F4100000`, with block 0 at `F4100400`;
- `CONFIG_CHIP_ID & 0xffff == 0x4C4D`;
- `MEM_CNTL.CTL_MEM_SIZEB` decodes to 4 MiB;
- Mobility FIFO availability is `GUI_STAT[25:16]`, not the pre-VTB
  `FIFO_STAT` occupancy bitmap;
- the part is affected by the VTB+ copy-commit and CPU-read cache hazards;
- a wedged engine can be reset with `GEN_GUI_RESETB` and full state replay.

ATI's *RAGE PRO and Derivatives Programmer's Guide* identifies Mach64LM as a
Mach64GT-core device with hardware 3D support and lists `4C4D` for Rage
Mobility M/P/M1. The *3D RAGE LT PRO Register Reference* supplies the compatible
Rage Pro-class setup, Z, alpha, texture and status register vocabulary. The
historical Mesa Mach64 DRI driver is the executable cross-check for state
translation and known fallbacks. These sources establish feasibility; only
measurements on the Gateway establish what Velocity9x may ship.

## Decisions fixed by this plan

| Question | Decision |
|---|---|
| Display ownership | Keep VBE mode setting for initial 3D. Native CRTC/panel programming is a later phase, not a first-triangle prerequisite. |
| Submission | Direct MMIO register writes with batched FIFO reservation. No AGP command DMA initially. |
| Texture memory | Local VRAM only. No AGP textures initially. |
| API architecture | One `V9X_D3D_ENGINE_OPS` implementation serves Direct3D and the neutral render interface used by OpenGL. |
| First public formats | RGB565/XRGB1555 targets, Z16, RGB565/ARGB1555/ARGB4444 textures. |
| Texture scope | One power-of-two 2D texture, nearest/bilinear, wrap/clamp, no mipmaps. |
| Unsupported state | Refuse before emission. Direct3D keeps skip-and-count; OpenGL may use whole-draw CPU fallback only after its safety gate passes. |
| VT2 | Remains non-3D. Common 2D code may serve it, but a VT2 must never acquire Mach64LM 3D caps. |
| Hardware writes | Private diagnostics first, one feature per armed build. No public capability bit before repeatable physical evidence. |

## Hardware capability boundary

The first engine may accept:

- flat and Gouraud shaded triangle lists;
- perspective-correct S/T using per-vertex W;
- RGB565 and XRGB1555 render targets;
- a 16-bit Z surface with all eight comparisons and independent write enable;
- alpha testing with all eight comparisons;
- blend equation ADD with supported source/destination factors;
- one RGB565, ARGB1555 or ARGB4444 power-of-two texture;
- nearest and bilinear min/mag filtering;
- wrap and clamp per axis;
- replace, modulate and alpha-decal texture environments;
- hardware scissor;
- fog only when blending is disabled.

It must initially refuse:

- stencil, accumulation and auxiliary buffers;
- mipmapping and trilinear filtering;
- the second texture input and multitexturing;
- texture borders, non-power-of-two textures, 3D/cube/rectangle textures;
- non-ADD blend equations or unsupported factor pairs;
- non-copy logic operations;
- fog plus blending;
- partial colour masks plus blending;
- 24/32-bpp render targets;
- AGP textures, VQ compression and bus-master command submission.

The silicon exposes more than this boundary. The boundary describes the first
implementation that can be made truthful and testable with 4 MiB VRAM.

## The 4 MiB constraint

Front, back and Z16 consume three 16-bpp surfaces before texture storage and
alignment:

| Mode | Front + back + Z16 | Approximate texture/scratch remainder |
|---|---:|---:|
| 640x480x16 | 1,843,200 bytes (1.76 MiB) | 2.24 MiB |
| 800x600x16 | 2,880,000 bytes (2.75 MiB) | 1.25 MiB |
| 1024x768x16 | 4,718,592 bytes (4.50 MiB) | Does not fit |

Therefore:

- 640x480 is the primary development and correctness mode;
- 800x600 may expose double-buffered Z hardware 3D with a smaller texture
  heap after alignment is measured;
- 1024x768 must not promise double-buffered Z hardware 3D. It may offer a
  deliberately reduced configuration later, or decline hardware 3D;
- every capability and surface-creation decision uses the current mode's
  measured heap, never the nominal 1024x1024 texture limit alone.

## Safety contract

Every physical-write phase follows the same contract:

1. An unarmed build is read-only and produces the pre-write fingerprint.
2. An arm token names the exact build, chip ID, PCI revision, mode and one
   bounded scene.
3. The diagnostic reserves off-screen VRAM and places guard patterns on both
   sides of every target.
4. FIFO waits have iteration and wall-clock bounds.
5. Timeout recovery performs the documented flush/reset/state replay once.
6. A second timeout, guard overwrite, display corruption or unexplained hard
   hang stops the phase; the next build may diagnose but not expand the write
   set.
7. The token is retired after one execution, pass or fail.
8. Captures record every intended register write in order, before execution,
   so a failed boot remains reviewable.

The laptop runs on AC power with a recoverable Win98 installation. The 86Box
VT2 is not accepted as evidence for any setup-engine, texture, Z, alpha or
Mobility FIFO claim.

## Phase 0 — freeze the interface facts and add a read-only fingerprint

Implement an ATI-only diagnostic publication with no engine writes.

Read twice and decode a small allowlist:

- PCI BAR0/BAR1/BAR2 and command bits;
- `CONFIG_CHIP_ID`, `CONFIG_STAT0`, `MEM_CNTL`;
- `BUS_CNTL`, `MEM_BUF_CNTL`, `GEN_TEST_CNTL`;
- `GUI_STAT`, `FIFO_STAT`, `GUI_CNTL` where present;
- destination, scissor, datapath, Z, alpha, texture and setup control
  registers needed by the first scene;
- active CRTC offset/pitch and LCD panel-size registers, without changing
  their index state.

Do not infer the register window from a successful mapping alone. Cross-check
the PCI ID against `CONFIG_CHIP_ID` and preserve/restore the indexed LCD
selector around LCD reads.

**Artefact:** `C:\V9XDIAG\ATIMM.TXT`, containing raw reads, repeat-read deltas,
decoded identity, VRAM size, FIFO model, active target and a `PASS`/`REVIEW`
verdict.

**Done:** two cold boots produce stable identity and memory decodes; the active
surface agrees with the VBE mode; reads are neither all-zero nor all-ones; no
register changes between the two quiet snapshots except identified live
status.

**Kill:** chip ID mismatch, an unexplained register-window alias, or a memory
decode contradicting the measured 4 MiB aperture.

## Phase 1 — shared Mach64 engine substrate

Add the code that every 2D and 3D operation will use, but publish no hardware
capability.

### Register and FIFO layer

- Add the reviewed Mach64LM register and mask subset to a project-owned
  header; do not import a historical driver's definitions wholesale.
- Map block 0 from the dedicated MMIO BAR on the Mobility and the correct
  in-aperture page on VT2.
- Reserve an exact number of FIFO slots before a batch.
- On Mobility, cache and decrement `GUI_STAT[25:16]`; re-read only when the
  cache cannot satisfy the next batch.
- On pre-VTB VT2, retain the separate `FIFO_STAT` population-count path.
- Wait for full idle only at CPU/framebuffer, readback, mode/power and recovery
  boundaries—not between ordered register batches.

### State and recovery

- Maintain one complete software shadow of engine state.
- Skip redundant state writes only after the baseline full-state path works.
- Implement bounded idle and one reset attempt: flush, pulse the active-low
  `GEN_GUI_RESETB`, replay all state, then recheck idle.
- Record timeout, reset and replay counters in shared diagnostics.
- Route all access through the existing Win16 mutex.

### CPU coherence

- Before a CPU read after engine activity, wait and set
  `INVALIDATE_RB_CACHE` in `MEM_BUF_CNTL`.
- Define the engine-to-CPU and CPU-to-engine boundaries once; the 2D, D3D,
  OpenGL fallback and screenshot paths must use the same helpers.
- Treat the documented Mobility screen-copy commit race separately: the first
  implementation waits idle after every screen copy until measurement proves
  a weaker rule safe.

**Tests:** host tests for address arithmetic, FIFO count decoding, batch
accounting, timeout bounds, reset ordering, state replay and generation
changes. A fake-MMIO transcript test must prove that a requested N-write batch
performs no status read between those N writes.

**Done:** the unarmed physical build fingerprints the engine and can exercise
the wait/recovery logic against injected software status without issuing a
draw command.

## Phase 2 — native 2D primitives, clears and presentation substrate

Implement conservative Mach64 operations behind private diagnostics first:

1. off-screen solid fill;
2. overlapping off-screen screen copy in all four direction combinations;
3. color clear using fill;
4. Z16 clear using a correctly sized/pitched depth surface;
5. back-to-front presentation by screen copy.

Use `DST_OFF_PITCH`, scissors, pixel-width/datapath state and the documented
last trigger write. Do not enable block write on the SDRAM board. Enforce the
Mobility post-copy idle workaround until a separate capture supports changing
it.

**Artefact:** `ATI2D0.TXT` plus small binary/BMP dumps, containing initial and
final CRCs, sentinel values, write transcript, FIFO reads, waits and recovery
counters.

**Done:** all five operations match CPU references across 640x480x16 and
800x600x16, guards remain intact, and 1,000 alternating operations complete
without timeout or display corruption across two cold boots.

After the private gate, wire `eng_mach64.c` into DirectDraw for solid fill and
screen copy. Flip and vblank remain unadvertised unless their own measurement
exists; copy presentation is sufficient for the first 3D release.

## Phase 3 — first off-screen triangle

This is the first setup-engine write and requires its own armed build.

Use 640x480x16 with a small off-screen RGB565 target:

- no texture, depth, alpha, blend, fog or dither;
- one opaque flat-colored triangle wholly inside the target;
- complete state emitted, with no state cache optimization;
- vertices converted from the neutral screen-space representation into the
  Rage setup engine's S/T/W, color, Z and X/Y formats;
- `SETUP_CNTL` and the documented final setup write initiate exactly one
  primitive;
- CPU readback occurs only after the shared drain/coherence boundary.

The first comparison is semantic, not necessarily a full CRC: assert interior
and exterior pixels, edges, target guards and untouched neighboring surfaces.
Once the fill convention is understood, promote a stable image/hash to the
regression corpus.

**Artefact:** `ATI3D0.TXT`, recording decoded and encoded vertices, all state
and setup writes, timing, status transitions and per-pixel assertions, plus a
target dump/BMP.

**Done:** the same reviewed scene passes on two cold boots, with correct
interior/exterior probes, intact guards, no error/timeout and a repeatable edge
rule.

**Kill:** progress requires undocumented register mutation without a bounded
hypothesis, or setup writes affect memory outside the target.

## Phase 4 — useful fixed-function 3D, one feature per build

Add one independently observable feature at a time. Each step has a CPU
reference scene, its own expected transcript and no expansion after a failure.

1. Gouraud color interpolation.
2. Z16 test without writes, all eight comparisons as table-driven host tests.
3. Z16 writes and clear.
4. RGB565 texture, nearest and clamp.
5. Perspective correction using unequal W values.
6. Wrap S/T and bilinear filtering, including seam and half-texel scenes.
7. ARGB1555 and ARGB4444 texture alpha.
8. Alpha test.
9. Blend ADD with each advertised factor pair.
10. Replace, modulate and alpha-decal texture environments.
11. Hardware scissor.
12. Fog without blending.

Texture upload uses local VRAM and pulses `TEX_CACHE_FLUSH` at the defined
visibility boundary. Texture allocation records offset, pitch, format and
generation; mode change or surface loss invalidates every resident copy.

The hardware may advertise a nominal 1024x1024 limit only if allocation tests
show it can bind such a surface in a mode where enough heap exists. Otherwise
publish the largest truthful per-mode limit or decline creation from heap
availability. No silent downscale or format substitution is permitted.

**Artefact:** `ATI3D1.TXT` with one record per scene and BMP/hash output.

**Done:** every advertised state has an isolated passing physical scene;
unsupported combinations are rejected before the first command; repeated
texture mutation proves cache visibility.

## Phase 5 — Direct3D engine integration

Add `V9X_DD_ENGINE_TYPE_ATI_MACH64` and a `d3d_mach64.c` implementation of the
current engine contract:

- `limits` describes the measured target, depth, coordinate, pitch, texture
  and alignment limits;
- `texture_format` accepts only the published formats;
- `describe_caps` publishes exactly the Phase 4 feature set;
- `ready` checks the mapped, fingerprinted, non-quarantined engine;
- `accepts` is passive and rejects every unsupported combination before
  emission;
- `draw` translates `V9X_R3D_DRAW`, reserves the complete batch and emits the
  setup registers;
- surface hooks are added only if DirectDraw's normal heap cannot guarantee
  the required placement/alignment.

### Required core correction: early capability selection

`v9x_d3d_publish_engine()` currently uses software when selected, recognizes
Gen3 when its type is already present, and otherwise falls back to ViRGE. A
second shipping hardware engine cannot inherit that behavior.

Before ATI exposes `V9X_DD_ENGINE_CAP_D3D`:

- stamp the chip's engine type early enough for `DriverInit` capability
  publication;
- make publication and runtime resolution use the same selector;
- fail closed for an unknown/unavailable type rather than publishing ViRGE;
- add host tests for every engine type and for mismatched caps/type;
- prove the ATI package's texture formats and caps contain no ViRGE-only
  promises.

### D3D gates

- Extend the Direct3D probe with untextured, Z, textured, alpha-test and blend
  scenes and refusal counters.
- Exercise context creation/destruction, target changes, texture swaps,
  surface loss/restore and mode changes.
- Run 3D WinBench 98 and Final Reality only after the probe passes. First
  record that hardware draw counters increase; a visual result alone does not
  prove the hardware path ran.
- Run windowed and fullscreen at 640x480 and 800x600. At 1024x768 verify the
  explicitly chosen reduced behavior or clean refusal.

**Done:** Direct3D publishes the conservative caps, every probe scene matches
the reference within its documented raster rules, unsupported state refuses
without partial emission, and named applications demonstrably submit hardware
draws across repeated restarts.

## Phase 6 — OpenGL hardware routing and software fallback

No ATI-specific ICD is added. The existing ICD already describes batches as
`V9X_R3D_DRAW`; once the Mach64 ops table implements `draw` and `accepts`, the
same feature set can run in hardware.

### Hardware route

- Extend render-interface `describe` output for the Mach64 limits, target
  format and accepted texture properties.
- Permit the ICD's retained CPU texture image to become a DirectDraw/local-
  VRAM texture surface in the accepted formats.
- Preserve the logical CPU image independently of residency for eviction,
  surface loss and mode changes.
- Ensure `flush`, `finish`, readback, clear and SwapBuffers use the shared
  bounded drain/coherence contract.

### Required core correction: fallback eligibility

`v9x_r3d_fallback()` currently allowlists the software renderer only for Gen3.
Do not merely add Mach64 to that conditional. First measure:

1. hardware RGB565 and XRGB1555 encodings against the CPU rasterizer;
2. hardware Z16 encoding and comparison direction against CPU Z;
3. hardware-to-CPU and CPU-to-hardware color/Z ordering;
4. texture upload visibility after CPU writes;
5. blended overlap across an engine boundary;
6. scissored whole-draw fallback with no double-rendered triangle.

Only an all-green matrix permits Mach64 whole-draw fallback. Otherwise the ICD
must describe only the hardware subset or use a separate CPU-owned frame and
explicit composite path; mixed rendering into one target would be unsafe.

### OpenGL gates

- `V9XGLP.EXE` obtains an ICD format and proves each Phase 4 state uses
  hardware counters.
- Compare its scenes with the generic OpenGL implementation and software
  Velocity9x backend.
- Run GLQuake and Quake 2 windowed and fullscreen, including context restart,
  `vid_restart`, front-buffer loading draws and both z-trick settings.
- Record hardware draws, fallback draws by reason, drain time, CPU time,
  texture uploads/evictions and screenshots.

**Done:** the probe is correct with hardware actually active; every fallback
is selected before emission; GLQuake and Quake 2 survive repeated context,
mode and surface-loss cycles without corruption or deadlock.

## Phase 7 — mode changes, presentation and native scanout decision

Hardware rendering may initially coexist with VBE mode setting, but live mode
switching must become an explicit ownership boundary:

1. drain the engine;
2. invalidate texture/surface residency and increment the render generation;
3. perform the VBE mode switch;
4. rediscover LFB, pitch, target format and available heap;
5. remap/revalidate MMIO if required;
6. reset or reinitialize the draw engine and replay baseline state;
7. restore DirectDraw/OpenGL surfaces or report loss for recreation;
8. redescribe the render interface before another draw.

Test 640x480x16 -> 800x600x16 -> 1024x768x16 -> 640x480x16, windowed/fullscreen
transitions, Alt-Tab, DOS-box entry/exit and Disable/Enable. A mode that cannot
fit the requested 3D surfaces fails creation cleanly rather than overlapping
the visible framebuffer.

After copy-based presentation is stable, decide separately whether native
CRTC/vblank/page-flip ownership is worth its panel and DSP risk. It is not
silently absorbed into this engine plan. If accepted, it needs its own
read-only timing capture and physical-write gate because the Mobility LCD
shadow registers and DSP calculation are not emulated by 86Box.

**Done:** every supported live transition either restores a usable hardware
context or reports surface loss for reconstruction, with no stale address
reused and no cross-mode VRAM overlap.

## Phase 8 — release gate

Release is a separate decision from successful diagnostics.

Required evidence:

- host suite and tree checks green;
- unarmed build performs no Mach64 engine write before a capability is
  deliberately enabled;
- two cold-boot passes of the complete 2D/3D diagnostic matrix;
- one-hour alternating D3D/OpenGL/2D stress at 640x480 and 800x600;
- repeated live mode switching and surface loss/restore;
- no FIFO timeout, reset, guard corruption or unexplained error status;
- hardware counters prove hardware execution for every advertised feature;
- unsupported features refuse or use a proven whole-draw fallback;
- clean uninstall/reinstall and rollback to the tier-0 ATI package.

Performance numbers are observations, not correctness gates. Record triangle
rate, texture upload bandwidth, FIFO reads per submitted register, idle/drain
time and fallback cost so later optimizations have a baseline.

## Representative file changes

The exact split may evolve, but responsibility should remain recognizable:

| Area | Expected files |
|---|---|
| Engine ABI | `include/velocity9x/engine_abi.h`, shared descriptor definitions |
| ATI register subset | `include/velocity9x/ati_mach64_regs.h` or equivalent project-owned header |
| 16-bit identity/early descriptor | `src/chipsets/ati/mobility/mobility_hw16.c`, `src/chipsets/ati/ati_hw16.c`, `src/display16/dd16.c` |
| Shared MMIO/FIFO/reset/coherence | `src/display32/engines/eng_mach64.c` plus a small internal header |
| DirectDraw engine selection | `src/display32/ddhal_core.c`, `src/display32/ddhal_internal.h` |
| Direct3D engine | `src/display32/d3d/d3d_mach64.c`, `d3d_internal.h`, `d3d_core.c` |
| Neutral render fallback | `src/display32/d3d/d3d_core.c`, `src/display32/r3d/*` only where the existing contract lacks a measured Mach64 case |
| Family declaration | `packaging/families/ati/family.psd1` |
| Diagnostics | `tools/diag/` and capture validators in `scripts/` |
| Host tests | FIFO/MMIO transcript, state translation, caps selection, VRAM layout, mode-generation and refusal tests under `tests/host/` |

Do not place Mach64 register or capability decisions in chip-neutral files when
an engine operation or limits field can carry them. Do not extend append-only
positional structs by inserting fields in the middle.

## Validation matrix

| Venue | What it can prove | What it cannot prove |
|---|---|---|
| Host tests | arithmetic, state translation, caps, validation, allocation, transcript ordering, refusal and recovery policy | silicon behavior |
| 86Box Mach64 VT2 | pre-VTB 2D register addressing, common 2D command construction, package and lifecycle regressions | Rage setup engine, Mobility FIFO depth/timing, cache hazards, LCD/DSP behavior |
| Gateway Solo 2150 | Mach64LM MMIO, FIFO, reset, 2D/3D results, VRAM pressure, cache coherence, panel/live switching | broad model/revision coverage |

Only the physical laptop can close a 3D phase. Evidence from one Mobility-M
revision must not silently enable other Rage PCI IDs.

## Risks and mitigations

- **A status-register mistake wedges the machine.** Exact-batch FIFO
  reservation, bounded waits, one reset, one-feature armed builds.
- **CPU reads stale VRAM.** One shared drain/invalidate boundary used by
  readback, fallback, screenshots and texture updates.
- **The copy-commit erratum corrupts presentation.** Idle after every copy
  until a dedicated physical experiment supports relaxing it.
- **Wrong early engine selection publishes ViRGE caps.** Remove the implicit
  ViRGE publication fallback before setting ATI's D3D capability bit.
- **4 MiB allocations overlap.** One mode-aware allocator, heap exclusion,
  guard regions and refusal at 1024x768 where the surface set does not fit.
- **Mixed hardware/software frames disagree.** No Mach64 fallback allowlist
  until color, depth, ordering and blend-boundary tests all pass.
- **VBE reprograms state behind the engine.** Mode changes are drain,
  invalidate, VBE set, rediscover, reinitialize, redescribe—not continuation.
- **Documentation covers close relatives rather than every LM quirk.** ATI
  guides establish the register model; every shipping claim is narrowed by
  physical measurement on `4C4D` revision `0x64`.

## Out of scope

- Mach64 VT2 hardware 3D—it has no Rage setup engine in the target model.
- Supporting additional Rage/LT/XL/Mobility PCI IDs without their own probe
  evidence.
- A separate ATI OpenGL ICD.
- AGP texture heaps, AGP command DMA and bus-master register lists.
- Two texture units or an OpenGL multitexture ABI revision.
- Mipmapping, trilinear filtering, VQ texture compression or 32-bpp targets.
- Native LCD timings, DSP programming and page flips before Phase 7 makes a
  separate ownership decision.
- Performance claims against ATI's stock driver before the correctness and
  stress gates pass.

## Source basis

- ATI, *RAGE PRO and Derivatives Programmer's Guide*, revision 1.0,
  March 2000: `PRG-215R3-00-10`.
- ATI, *3D RAGE LT PRO Register Reference*, revision 2.01x, June 1998:
  `RRG-G03300`.
- X.Org ATI/Mach64 driver documentation:
  <https://www.x.org/releases/X11R7.0/doc/html/ati3.html>.
- Local MIT-licensed X.Org Mach64 source at `C:\everything\xf86-video-mach64`.
- Historical Mesa Mach64 DRI source at
  `C:\everything\claude\personal\3dfx driver research\artifacts\MesaFx-7.4.4\src\mesa\drivers\dri\mach64`.
- Velocity9x hardware audit:
  [`../decisions/2026-08-16-ati-mach64-hardware-audit.md`](../decisions/2026-08-16-ati-mach64-hardware-audit.md).
- Shared OpenGL/render architecture:
  [`opengl-1.1-icd.md`](opengl-1.1-icd.md).


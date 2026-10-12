# VSA-100 Phase 5: a Direct3D engine for the Voodoo4 (OpenGL follows)

Date: 2026-10-12

Status:
- **Done:** step 0 (fact sheet); step 1 (`tdfx_3d.c`, host-tested); step 2
  (the probe: flat, Gouraud, depth, blend and texture all pass on the 4500
  at the first run;
  [2026-10-12-voodoo4-3d-engine-draws-pixel-exact.md](../decisions/2026-10-12-voodoo4-3d-engine-draws-pixel-exact.md)).
- **Step 3, the engine:** built, and passes V9XDDP's core on the 4500
  ([2026-10-12-voodoo4-direct3d-engine-passes-v9xddp-core.md](../decisions/2026-10-12-voodoo4-direct3d-engine-passes-v9xddp-core.md)).
  The control run under 3dfx's driver differs only in features this engine
  refuses or lacks.
- **Half-Life (step 5's gate game):** draws correctly
  ([2026-10-12-voodoo4-half-life-direct3d-on-velocity9x.md](../decisions/2026-10-12-voodoo4-half-life-direct3d-on-velocity9x.md)).
  Missing is mipmapping: Half-Life asks for MIPLINEAR. 25.7 fps against
  3dfx's 58.9 is recorded, not chased.
- **Next:** mipmaps, then OpenGL (step 4).

Parent plan: [3dfx-vsa100-family.md](3dfx-vsa100-family.md),
Phase 5. Register facts: [vsa100-registers.md](../specifications/vsa100-registers.md)
(its 3D part is names only today; step 0 fills it in). Boundaries:
[win9x-driver-boundaries.md](../specifications/win9x-driver-boundaries.md).
Models: [matrox-mga2164w-hardware-3d.md](matrox-mga2164w-hardware-3d.md) and
[sis-6326-hardware-3d.md](sis-6326-hardware-3d.md), whose shape this plan
follows.

## What is already true

**The engine interface.**
- Every hardware Direct3D engine implements `V9X_D3D_ENGINE_OPS`
  (`d3d_internal.h`): `limits`, `describe_caps`, `ready`,
  `create_surface`/`destroy_surface`, `draw` (a neutral batch of at most
  64 screen-space triangles, `V9X_R3D_VERTEX` floats), and `accepts`.
- MGA and SiS split each engine into a host-tested `*_map.c` (a draw
  becomes chip state, or a refusal reason) and an engine file that only
  places surfaces and writes registers.
- OpenGL and the Glide wrappers reach the same engine through
  `V9xRenderInterface`. OpenGL on this card therefore needs a render-core
  descriptor entry, not chip work.

**Selection and advertising.**
- Selection: `d3d_select.c` sends TDFX to "none" today, and
  `test_d3d_select.c` asserts it.
- The 16-bit side advertises Direct3D only with `CAP_D3D` in the engine
  caps, a `direct3d` string beginning `hardware-`, and the manifest's
  `Direct3D` key and `D3D` cap. `check-tree` holds those three in
  agreement.

**Present.** TDFX has no `CAP_FLIP`, so a Direct3D application's Flip is
declined and DirectDraw presents by copying, through the 2D engine.

**The chip** (Napalm spec rev 1.13).
- **Direct writes are a supported path.** The 3D registers sit at
  BAR0 + 200000h. The triangle setup unit takes IEEE floats per vertex:
  `sVx`, `sVy`, `sARGB` or `sRed..`, `sVz`, `sWb`, `sS/W0`, `sT/W0`.
  `sBeginTriCMD` and `sDrawTriCMD` draw strips and fans; `sSetupMode`
  says which parameters are sent.
- **Two TMUs per chip.**
- **Linear colour and depth buffers are allowed.** Bit 15 of each stride
  selects tiled; the base is 16-byte aligned.
- **Textures:**
  - formats include 565, 1555, 4444, 8888 and P8;
  - up to 256 texels a side, or 2048 with `tLOD[30]`;
  - `texBaseAddr` bit 0 means tiled, the address is 16-byte aligned, and
    a single base points where a full-size level 0 would start.
- **Idle:** write `nopCMD`, then read status bit 9 clear three times.

**Glide coexistence (Phase 4).**
- The desktop and DirectDraw's heap live from 15 MiB up. 3dfx's Glide
  owns the 14 MiB below, and its command FIFO sits at 18000h.
- While Glide holds the card, the HAL's engine-valid flag is clear.

**VelocityNT** renders no 3D on any chip. Its M6 plan for the VSA-100 is
the same as this one: direct writes with bounded waits; untextured, then
depth, then blend, then textures with readback; the command FIFO later; a
linear RGB565 surface first.

## Decisions (proposed; Michael confirms the marked ones)

1. **Direct writes to the setup unit, no command FIFO.** One register write
   per parameter, `sDrawTriCMD` per vertex, PCI FIFO free space polled
   (`status[4:0]`) before each burst. The command FIFO is a later, separate
   step, and only after direct submission is the reference; it is also the
   thing Glide owns.
2. **Linear 16-bit colour and Z16 in DirectDraw's heap**, 16-byte aligned.
   No tiled surfaces in this phase. Retro-3dfx's colour/depth page-parity
   rule was measured not to bind linear buffers (D36, boot 428), so
   DirectDraw may place them where it likes.
3. **One TMU (TMU0), single-base linear textures up to 256**, formats 565,
   1555, 4444; point and bilinear; modulate, decal, add. 2048 textures,
   the second TMU, P8 and compressed formats are later steps.
4. **Glide owns the 3D state.** After every exclusive release the engine
   re-sends its whole state before the next draw (a generation count the
   release bumps). While Glide holds the card the engine is not ready and
   draws refuse, as the 2D engine already does.
5. **Present stays DirectDraw's copy.** Hardware flip through
   `vidDesktopStartAddr` (with the desktop's 15 MiB offset) is a later step
   with its own measurement.
6. **The gate game is Half-Life on its Direct3D renderer** (Michael,
   2026-10-12). It is already on A8U4I5 with `mwd5.dem`, and it gated the
   Matrox. It must first be confirmed to use hardware Direct3D under 3dfx's
   own driver.
7. **The OpenGL/Direct3D performance review runs after this engine draws**
   (Michael, 2026-10-12). Both legs are then run together: GMA 950 and
   Rage XL, and the tdfx engine.

## Steps

0. **Desk: fill in the fact sheet.** The 3D section of
   `vsa100-registers.md` gets the setup unit, `fbzColorPath`, `alphaMode`,
   `fbzMode`, `fogMode`, `textureMode`, `tLOD`, `texBaseAddr`, the colour
   and aux buffer registers, `renderMode` and the idle rule, with spec
   citations and the disagreements the extraction found:
   - the fan bit, 0 in the prose and 16 in the table;
   - how packed versus separate colour is chosen under direct writes;
   - Z and W scaling;
   - PCI FIFO back-pressure;
   - the `combineMode` field width.
   No code.
1. **Pure builder, host-tested: `tdfx_3d.c`.** From a neutral description
   (target, depth, state, three vertices) it produces the ordered
   `(offset, value)` words: state registers, then `sSetupMode`, then per
   vertex the parameters and `sBeginTriCMD`/`sDrawTriCMD`. Tests are
   written red first, against vectors computed by hand from the spec.
2. **Probe `TDFX3D.EXE`** (beside `TDFX2D.EXE`), on A8U4I5 with Michael able
   to reset. It maps the 3D register window through the mini-VDD (a new
   function, or BAR0's 32 MiB map that Glide already uses), and draws into
   a scratch surface it owns at the top of the heap. It reads the result
   back and compares it with a CPU reference.
   - **2a:** flat, then Gouraud, triangle.
   - **2b:** Z16 compare and write.
   - **2c:** alpha blend.
   - **2d:** a 565 texture uploaded by the CPU, point then bilinear,
     modulate.
   Every sub-step is bounded by idle checks before and after and writes
   nothing outside its scratch surface. Each passing sub-step gets a
   decision record with its captures; a failing one records what the
   evidence killed.
3. **Engine `d3d_tdfx.c` + `d3d_tdfx_map.c`.** `create_surface` places
   textures (16-byte aligned) and leaves targets and Z to DirectDraw.
   `draw` maps a batch, writes it, notes submission. The rest:
   - quarantine on a bounded timeout, as MGA and SiS do;
   - selection rows and their host test;
   - `CAP_D3D` stamped by `vsa100_hw16.c` and the 3D window mapped
     beside the 2D block;
   - the manifest's `Direct3D='hardware-vsa100'` and `D3D` cap.
   Gate: `V9XDDP`'s Direct3D sections on the 4500, beside the same run under
   3dfx's own driver (the control).
4. **OpenGL.** The render-core descriptor entry (engine id, hardware texture
   shape). Gate: `V9XGLP` and Quake 2, as on the other engines.
5. **Applications.** The gate game (decision 6); then the parent plan's
   Half-Life mwd5 comparison on both renderers beside the stock driver's
   numbers, recorded, not chased.

Later steps, each with its own record:
- 32-bit colour with 24-bit Z and stencil;
- the second TMU;
- 2048 textures;
- P8 and FXT1/DXT;
- fog and specular;
- hardware flip;
- the command FIFO;
- tiled buffers.

## Safety contract

- Every wait is bounded. A timeout quarantines the engine for the mode
  (clears VALID) and records the status and FIFO registers.
- Probes touch only their own scratch surface and the 3D registers, check
  idle before and after, and never run while a Glide title is open.
- No sweep of 3D registers. Values come from the fact sheet, one change
  at a time.
- A probe step that hangs the machine costs a hardware reset. Probes run
  only when Michael is at A8U4I5.

## Open, hardware answers

- Does a direct-write burst need PCI FIFO polling, or does PCI retry make it
  safe?
- How does the setup unit choose packed `sARGB` over separate colour without
  the command-FIFO packet bit?
- Are Z and W what the spec implies: depth counts and 1/W?
- ~~Does the 4500 render correctly into a linear target at any page
  parity?~~ Yes (TDFX3D /parity, boot 428).
- How many TMUs does Glide report on the 4500? The spec says two per chip.

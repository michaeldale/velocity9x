# HD 5770 (Juniper) bring-up: registers to Quake 3 in one pass

Date: 2026-11-01
Status: **Phases 1 to 6 of the plan done for Juniper** on M-BXMFA8's HD 5770
(`1002:68B8`), boot 49. DirectDraw on the DMA engine, flips, Direct3D and
OpenGL on the CP; V9XD32 passes at 16 and 32 bpp; the owner's Quake 3 demo
runs at their 1024x768x32 settings with every batch on the engine.
Plan: [ati-evergreen-juniper-caicos.md](../plans/ati-evergreen-juniper-caicos.md).
Captures: [`docs/probe/m-bxmfa8-juniper-registers-2026-11-01/`](../probe/m-bxmfa8-juniper-registers-2026-11-01/),
[`…-dma-2026-11-01/`](../probe/m-bxmfa8-juniper-dma-2026-11-01/),
[`…-cp-2026-11-01/`](../probe/m-bxmfa8-juniper-cp-2026-11-01/),
[`…-d3d-2026-11-01/`](../probe/m-bxmfa8-juniper-d3d-2026-11-01/).

## Registers (V9XEGMM, boot 48, 0.33.0 vbe)

| | Juniper (M-BXMFA8) |
|---|---|
| BAR0 / BAR2 / BAR4 | `D000000C` (64-bit) / `FEAE0004` / I/O `D000` |
| `CONFIG_MEMSIZE` | 1024 MiB, 4 channels, 128-bit |
| Memory | `MC_SEQ_MISC0` type GDDR5, sequencer **running** (so Linux would not load MC microcode either) |
| `MC_VM_FB_LOCATION` | `003F0000`: VRAM at GPU 0, like Cedar (no high byte) |
| CP / DMA | CP halted; DMA not in reset, idle |
| SIMD disable (`CC_GC_SHADER_PIPE_CONFIG` 31:16) | `FC00`: all ten SIMDs |
| `CC_RB_BACKEND_DISABLE` 23:16 | 0 |

Linux takes Juniper's backend-disable mask from the RCU fuse straps,
behind an index register this read-only probe does not write. The map
below assumes none disabled, which a full HD 5770 should be; V9XD32's pixel
checks passing is the evidence it is right.

## DMA and CP probes

V9XEGDMA's and V9XEGCP's VxDs now accept `68B8`. V9XEGCP sends Juniper's
eight hardware contexts in `ME_INITIALIZE` (7), keyed on the id the VxD
reads.

| | Result |
|---|---|
| V9XEGDMA, five runs (window GPU `0F000000`) | fences land; dword and byte copies and the fill byte-exact; overlap `memmove`; every run |
| V9XEGCP with `JUNIPER_pfp.bin` / `JUNIPER_me.bin`, three runs (window `0F100000`) | `ME_INITIALIZE` PASS, ring test PASS (`SCRATCH_REG0` `DEADBEEF`), CP halted again; every run |

Juniper's microcode (linux-firmware, SHA-256 `5f4e98e1...a9e2` PFP,
`38904378...c9a0` ME) has Evergreen's sizes and differs from Cedar's and
Caicos's.

## The driver

- `V9X_DD_ENGINE_CAP_EG_JUNIPER` (bit 25), stamped by a third row in
  `cedar_hw16.c` (`v9x_evergreen_juniper_device`, the row-per-chip shape
  chosen for Caicos), claiming fill, copy, flip and D3D.
- Juniper's microcode generated beside Cedar's and Caicos's.
- `v9x_eg_juniper_gpu_init`: Cedar's table entry for entry with Juniper's
  `evergreen_gpu_init` numbers: `GB_ADDR_CONFIG` `02010002`,
  `GB_BACKEND_MAP` `00003210` (Linux's `r6xx_remap_render_backend` for
  four pipes and four enabled backends; the same arithmetic gives Cedar's
  0), SX export sizes `002F0F3F`, PA_SC FIFO `13030100`, SQ MS FIFO
  `08E00420`, `SQ_CONFIG` with `VC_ENABLE`, threads `10101080` /
  `00001010`, stacks `00550055`, VGT cache `VC_AND_TC` (`C2`).
- `ME_INITIALIZE` takes the chip (8 contexts on Juniper). Linux 3.9's
  `set_default_state` for Juniper (threads `14141480` / `00001414`, stacks
  `00550055`, `VC_ENABLE`) is the default state's Juniper branch.
- A batch on Juniper syncs its vertices with `VC_ACTION` rather than
  `TC_ACTION`, as Linux 3.9's `set_vtx_resource` does on parts with a
  vertex cache (`v9x_eg_3d_state.vertex_cache`).
- The renderer is named "Velocity9x Radeon HD 5770".

Host tests first, failing to compile against the old interfaces: Juniper's
table against Cedar's with each changed value, the default state's Juniper
words, `ME_INITIALIZE` contexts, the vertex-cache sync changing exactly one
dword, and the microcode's sizes and end dwords. `run-checks.ps1` passed.

## Boot 49

`V9XDISP.DRV`, `V9XMINI.VXD`, `V9XHAL.DLL`, `V9XGL.DLL` by WININIT
renames over the vbe INF's registry, then `reset` (48 to 49); each matched
by hash. Probes from the same build in `C:\V9XDIAG`.

| | Result |
|---|---|
| `V9XHW.INI` | `Adapter=AMD Radeon HD 5770 (Juniper)`, engine claimed, `EngineCaps=02000017`, `Direct3DMode=hardware`; `DpmiMap=08000000:ok` |
| V9XD32 at 1024x768x16 and x32 | `Result=PASS`, `Failures=0` both |
| V9XDDP | `COMPLETE`; 93 / 13 and 121 / 1, the same counts as Caicos |
| Quake 3 demo at the owner's settings (1024x768, 32-bit colour and textures, `r_picmip 0`, fullscreen), `timedemo 1`, `demo001` | **1346 frames, 27.3 s: 49.3 fps**; `GL_RENDERER` "Velocity9x Radeon HD 5770", 32-bit colour, 16-bit Z |
| ICD paths | hw 211,851 batches / 5,419,687 triangles; cpu 0; refused 0; 355 textures, no failures |
| V9XTRACE after | `Ok=1`, `BuildsAgree=1`, flips 31 handled, 0 declined, 29 taken at done |

The owner's `q3config.cfg` was backed up before and restored after (hash
matches). The desktop afterwards is clean.

## Not established

- The owner's open reports from the HD 5450 (`Issues.txt` on the desktop):
  3DMark 2000 Helicopter trees, Quake 3 after an in-game resolution change
  at 32 bpp, 1-2 fps at 1280x1024 and above, 3DMark 2001 plants. None was
  re-run on this card.
- Vertical-blank pacing and what the monitor shows.
- Juniper's fifth and sixth display controllers (D1 is the one used).
- The RCU fuse straps.

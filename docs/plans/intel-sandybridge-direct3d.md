# Direct3D on Sandy Bridge (Gen6, HD 3000)

Status: **planned 2026-11-19, nothing built.** Step 7 of
`docs\plans\intel-sandybridge-gen6.md`. Machine: M-NZHWBR (i3-2105, HD 3000
GT2 `8086:0112`), which runs `hd3000-snb`'s 2D, flips and RP0 since boot 62.

## Goal and scope

Ivy Bridge's scope, on Gen6: pre-transformed (DDI 5-7 TL) vertices, one
texture stage, Z16 and Z24, 16 and 32 bpp, V9XD32 passing, then 3DMark 2000
and the OpenGL ICD's Quake 2/3 paths that run on the HD 4000. Not in
scope: hardware T&L, DDI 8 pixel shaders, the texture pool, two texture
stages in one pass. Each can follow once the base works, as on Ivy Bridge.

## What the Gen7 path is (survey, 2026-11-19)

- `hsw_3d.c` builds every batch in one function, `v9x_hsw_workload`
  (`:1934`), from a command table (`:46-92`: subtype, opcode, subop,
  length) that `v9x_intel_hsw_3d_emit` checks lengths against, and from
  word builders for each 3DSTATE and indirect state object. Pure C with
  host tests (`test_intel_hsw_3d.c`, a batch checker `:1126`).
- Ivy Bridge differs by a file-static chip byte
  (`v9x_intel_hsw_3d_set_chip`, `:96`), which six sites read: the every-4th
  CS stall, SURFACE_STATE DW7, 3DSTATE_PS DW4, the VS flushes and the dummy
  draw after PIPELINE_SELECT.
- Kernels: `hsw_eu.c` encodes Gen7 instructions; the fixed-function PS
  (`:782`, SIMD8, PLN, two samples, RT write from g112-115) and a
  position-only VS (`:981`, URB write) are what Ivy Bridge runs. T&L
  (`hsw_vs.c`) and PS 1.1 (`hsw_ps11.c`) are Haswell's alone.
- The HAL (`d3d_hsw.c`) owns submission, slots, surfaces and the
  D3D-to-state mapping. It selects the Gen7 table by `CAP_GEN7_IVB`
  (`d3d_select.c:73`), and `ready` (`:594`) requires `CAP_D3D`, which
  `hsw_hw16.c` withholds from Sandy Bridge today.

## What Gen6 changes (Mesa 24.0.9 genxml, crocus, brw; Linux 6.6 i915)

### Commands

| Gen7 | Gen6 |
|---|---|
| URB_VS/HS/DS/GS, PUSH_CONSTANT_ALLOC_* | One `3DSTATE_URB` (`7805h`, 3 dwords; **Gen7's DEPTH_BUFFER opcode**). No push-constant allocation |
| HS, DS, TE, STREAMOUT | Do not exist: not emitted |
| `3DSTATE_GS` | 7 dwords, enable in DW5 bit 15 |
| `CONSTANT_VS/PS` (7 dwords) | 5 dwords; buffer-valid bits in DW0 15:12; each buffer one dword, pointer 31:5 and read length - 1 in 4:0 |
| `SF` + `SBE` | One `3DSTATE_SF`, 20 dwords: Gen7 SBE's DW1 fields in DW1, Gen7 SF DW1-6 in DW2-7, swizzles DW8-15, flat mask DW17 |
| `WM` + `PS` | One `3DSTATE_WM`, 9 dwords: kernel pointers DW1/7/8; samplers and binding entries DW2; dispatch GRF DW4; dispatch enables, max threads 31:25, kill, DW5; barycentric mode 15:10 and **SF output count 25:20** in DW6 |
| Per-stage BINDING_TABLE, SAMPLER pointers | Combined, 4 dwords each, change bits 8/9/12 |
| VIEWPORT_STATE_POINTERS_SF_CLIP, _CC | Combined `780Dh`, 4 dwords: CLIP, SF, CC |
| BLEND, DEPTH_STENCIL, CC pointers | All in `3DSTATE_CC_STATE_POINTERS` (`780Eh`, **4 dwords**; Gen7's is 2) |
| DEPTH_BUFFER `7805h`, STENCIL, HIER_DEPTH, CLEAR_PARAMS | `7905h`, `790Eh`, `790Fh`, `7910h` (2 dwords) |
| MULTISAMPLE (4 dwords) | 3 dwords |
| 3DPRIMITIVE (7 dwords) | **6 dwords; topology in DW0 14:10, access type DW0 bit 15** |
| PIPE_CONTROL | Same length; no DC flush; the GGTT bit is DW2 bit 2, not DW1 bit 24 |

STATE_BASE_ADDRESS, PIPELINE_SELECT, VERTEX_BUFFERS (no address-modify
bit) and ELEMENTS, DRAWING_RECTANGLE, SCISSOR pointers, SAMPLE_MASK and
CLIP (DW1 lost winding and cull, which move to SF) keep their opcodes.
crocus sets STATE_BASE_ADDRESS's dynamic upper bound to `FFFFF000`: zero
breaks border colours.

### Indirect state

- SURFACE_STATE: **6 dwords**; tiling in DW3 bits 1:0, pitch DW3 19:3,
  width/height DW2. No channel selects, no HALIGN.
- SAMPLER_STATE: 4 dwords with fields moved (shadow function 2:0, LOD bias
  13:3, min/mag-not-equal bit 27); max LOD 13. Border colour is the
  12-dword `SAMPLER_BORDER_COLOR_STATE`.
- Viewports are separate: SF_VIEWPORT 8 dwords, CLIP_VIEWPORT 4 (the guard
  band), CC_VIEWPORT 2. BLEND, DEPTH_STENCIL and COLOR_CALC are Gen7's.
- Depth buffer: no write-enable bits (DEPTH_STENCIL_STATE alone); D16,
  D24X8, D24S8 combined, D32F; Y-tiled. Separate stencil and HiZ off.

### Rules

- **Post-sync non-zero flush** before any PIPE_CONTROL with an RT flush and
  before any depth stall: `CS_STALL | STALL_AT_SCOREBOARD`, then a
  `WRITE_IMMEDIATE` to a scratch dword (crocus `crocus_pipe_control.c:229`).
  The status page's IVB workaround dword can be the target.
- Depth stall never with an RT or depth flush; depth flush never with a
  depth stall; a CS stall always with a companion bit. Drop Gen7's
  every-4th CS stall and the VS flushes.
- Before depth buffer state: depth stall, depth flush, depth stall.
- After 3DSTATE_VS: depth stall plus instruction and state cache invalidate.
- 3DSTATE_SF's URB read length must be exactly what the VS writes (crocus
  notes a hang otherwise).
- Render-ring register settings from i915 (`intel_workarounds.c:338, 2853+`):
  INSTPM force ordering; MI_MODE async-flip and VS timer dispatch; GFX_MODE
  explicit TLB invalidate; `_3D_CHICKEN3` fast-clip cull off and pipelined
  attribute fetch off; GT_MODE WIZ hashing 16x4; CACHE_MODE_0 RC op flush
  off, STC evict LRA off. The Gen7 path sets none of its own; whether Gen6
  runs without them is to be measured, starting with them set.
- URB: VS entries a multiple of 4, size in 128-byte units, the whole 64 KiB
  to the VS with no GS. GT2: VS 60 threads, WM 80.

### Shaders

- **MRFs.** A Gen6 send's payload is in message registers (m0-m15), not
  GRFs; the encoder must emit MOVs to the MRF file and sends that take it.
  Gen7's "MRF as g112-127" layout maps directly.
- Send descriptors move: RT write type at 16:13 (Gen7 17:14), message
  control 12:8; sampler type 15:12 and SIMD mode 17:16 (Gen7 16:12,
  18:17); URB write opcode 3:0, offset 9:4, allocate 13, used 14, complete
  15, and no `FF00h` channel mask in the header.
- MATH is a native instruction (GRF destination, no source modifiers).
  PLN exists. Three-source instructions are Align16 and float-only.
  Gen6 has no JIP/UIP control flow (none of these kernels branches).
- The PS thread payload (SNB PRM Vol 2 Part 1, 7.7.3.1): R0 header, R1
  pixel X/Y and masks, R2 only for 32-pixel dispatch (otherwise skipped
  and the rest renumbered), then each enabled barycentric mode (perspective
  pixel: two registers in SIMD8), source depth/W if asked for, then padding
  up to `3DSTATE_WM` DW4's Dispatch GRF Start, push constants, and the
  setup data in Gen7's layout: two channels of one attribute per register,
  a1/a2/a0 per channel, which PLN reads. So the fixed-function PS's g2-g3
  barycentrics and its setup-data base carry over unchanged.

## Sources

Intel's Sandy Bridge PRM, all 13 volumes from Intel's own documentation
page (cdrdv2.intel.com, 2026-11-19), in
`C:\everything\claude\personal\intel driver research\sandybridge-2011-gen6-prm\`,
with `pdftotext -layout` copies of Vol 1 Part 2-3, Vol 2 Part 1 and Vol 4
Part 1-2 beside them. The x.org mirror answers with a bot challenge now.

Answered from it:
- The PS payload, above.
- Depth: "[DevSNB]: [Separate Stencil Buffer Enable] must be set to the
  same value as Hierarchical Depth Buffer Enable" (Vol 2 Part 1,
  3DSTATE_DEPTH_BUFFER DW1 bit 21). Both off: D16, D24_UNORM_X8 and
  D24_UNORM_S8 (combined stencil) are all usable; with HiZ on the format
  cannot carry stencil.

## Design

1. **A Gen6 command table and builders beside Gen7's, in a new
   `src\chipsets\intel\snb_3d.c`**, not branches inside `hsw_3d.c`'s.
   Nearly every 3DSTATE the workload emits changes length or layout, and
   two opcodes mean different things on the two chips, so a chip byte
   inside each builder would put Gen6 words one test away from Gen7
   batches. Shared: the batch type, MI commands, STATE_BASE_ADDRESS,
   vertex buffers and elements, and the state that is the same (BLEND,
   DEPTH_STENCIL, COLOR_CALC, binding table). `v9x_intel_hsw_3d_emit`
   takes the table as an argument.
2. **Its own `v9x_snb_workload`**, the Gen6 emission order (crocus's),
   built for the TL draw and the init batch only.
3. **The encoder learns Gen6 sends**: an MRF register file and the three
   descriptors, behind a chip argument to the send builders; the Gen7
   kernels' instruction words stay as they are, and host tests pin both.
4. **The HAL** gets a third ops table, `v9x_d3d_engine_snb`, with Ivy
   Bridge's limits, selected by a new identity bit `CAP_GEN6_SNB`
   (`engine_abi.h`), since `CAP_GEN7_IVB` already means "Ivy Bridge's
   plane" on Sandy Bridge. Surfaces, slots and submission are reused.
5. **`hsw_hw16.c`** stamps `CAP_D3D` and the new bit for chip 2, and the
   manifest's Direct3D word becomes `hardware-gen6`, only when V9XD32
   passes.

## Order, each step with its gate

1. **Sources.** Ask Michael to allow downloading Intel's Sandy Bridge PRM
   (2011, Vol 1-4) into the personal research folder: the PS payload
   layout and the separate-stencil/HiZ rule are not in Mesa's genxml.
2. **Gen6 table and builders, host-tested** against gen6.xml's field
   positions, with a Gen6 batch checker: every command's length and
   opcode, the post-sync flush before each RT flush and depth stall, no
   Gen7-only command. Gate: build-host.
   *First slice done 2026-11-19* (`src\chipsets\intel\snb_3d.c`,
   `tests\host\test_intel_snb_3d.c`): the 27-command table and header,
   PIPE_CONTROL (GGTT moved to DW2, DC flush, lone CS stall and
   depth-stall-with-flush refused) and the post-sync non-zero pair, URB,
   VS and GS off, CLIP, the 20-dword SF, the 9-dword WM, CONSTANT_PS, the
   four combined pointer commands, DEPTH_BUFFER, CLEAR_PARAMS,
   MULTISAMPLE, 3DPRIMITIVE, a 6-dword SURFACE_STATE and the SF and CLIP
   viewports. Every expected word worked by hand from the field
   positions. *Rest done 2026-11-20:* SAMPLER_STATE from the Gen7
   description (LODs to U4.6, clamped to 13; address modes from DW3 to
   DW1), vertex buffers without bit 14, the init state image, the init
   and TL draw workloads (depth with or without a clear, cull, flat,
   scissor, second UV, indexed list or strip) in crocus's Gen6 order, and
   the checker, which refuses Gen7's lengths, 3DSTATE_SBE, an RT flush or
   depth stall without the post-sync pair, and a primitive before the
   state. Every workload test batch passes it. Border colours are left
   zero (12 dwords at Gen7's 0x360 slot fit before 0x400).
3. **Gen6 sends and MRFs in the encoder**, host-tested; the
   fixed-function PS and position VS built for Gen6 and run in the EU
   simulator where it can model Gen6 (else word-for-word tests).
   *Done 2026-11-20 for the PS* (`hsw_eu.c`, `test_intel_hsw_eu.c`):
   `v9x_intel_snb_eu_encode` allows the MRF file (10b, 24 registers) as
   a non-send, non-math destination and a send's src0 only; the Gen6
   sampler (SIMD 17:16, type 15:12) and render-cache (type 16:13)
   descriptors, checked against the SNB PRM's descriptor tables and
   Mesa's Gen6 field positions; `v9x_intel_snb_ps_kernel_build`, Gen7's
   program with each sample's u, v MOVed to m1-m2 and the colour MOVed to
   m1-m4 before a header-less EOT write. Word-for-word tests, worked by
   hand; the EU simulator models Gen7 only and was not extended. The
   position VS is left Gen7-only: TL draws run with the VS off, and
   T&L is out of this plan's scope.
4. **Init batch on hardware**: the HAL submits the Gen6 init batch with
   the workaround registers and waits for its breadcrumb, nothing drawn.
   Record the ring, ACTHD and breadcrumb. A hang here is the cheapest one
   to diagnose.
   *Done 2026-11-20, without the workaround registers*
   (`docs\decisions\2026-11-20-sandybridge-gen6-init-batch.md`): boot 63,
   `SyncSubmits` 1, breadcrumb stored back, no timeout. `CAP_GEN6_SNB`
   picks the Gen6 builders; `ready()` declines every draw after the
   setup batch until step 5 wires the draw workloads.
5. **One untextured triangle** (V9XD32's first stage) at 32 bpp, no Z,
   then with Z16 and Z24.
6. **Textures**, one stage: V9XD32 at 32 and 16 bpp.
   *Steps 5 and 6 done 2026-11-20* (`docs\decisions\2026-11-20-
   sandybridge-gen6-direct3d-v9xd32.md`): its own engine selection and
   limits (no DXT), the HAL's surfaces, mips, samplers, kernels and draw
   batches switched to Gen6 by the engine bit; V9XD32 passes at 32 and 16
   bpp and under DDI 6 and 5 with Z16, 4523 asynchronous batches, no
   timeout, still without the i915 workaround registers. *Owed:* Z24.
7. **Games**: 3DMark 2000 (score recorded, not compared), the OpenGL ICD
   with Quake 2/3, windowed for screenshots.
   *Probes and 3DMark 2000 done 2026-11-20*
   (`docs\decisions\2026-11-20-sandybridge-3dmark2000-and-probes.md`):
   every release probe passes (V9XD32 x4, GL, Glide, R3DP, WPRS, FBCMP),
   3DMark 2000 4015 with clean counters and grabs. *Owed:* Quake 2/3,
   Glide games, Z24.
8. Then, separately: T&L, PS 1.1, two stages, the texture pool.

## Risks

- A malformed Gen6 batch hangs the GPU, and M-NZHWBR is remote: a hang
  that `reset` does not clear needs the owner. Step 4 exists to take that
  risk with the smallest batch.
- The PS payload layout is unverified until step 1 or 5.
- The i915 render-ring workarounds are register writes the Gen7 path has
  never made; each is named and set in one place, so one can be dropped
  at a time if it misbehaves.

# Radeon HD 7660D (Trinity, the A10-5700's graphics) on the ati family

Date: 2026-11-03

Status: Phases 0 and 1 (read) done on M-D7TFW4, boot 17
([record](../decisions/2026-11-03-trinity-tier0-and-registers.md)). The
owner made the APU's graphics the display and installed 0.34.0 `vbe`;
GDI, mode and DirectDraw tests pass. The fuses read six SIMDs and two
backends, as Linux's `9901` row says, with the 256 MiB carve-out at GPU
`F_00000000`. Branch `ati-trinity`. Phase 2's write probe done
(2026-11-03, [record](../decisions/2026-11-03-trinity-dma-write-probe.md)):
DMA0 byte-exact five of five, with `SRBM_SOFT_RESET` bit 16 (set by the
BIOS, unnamed in Linux) kept as found. Phases 2 and 3 done (2026-11-03,
boot 18, [record](../decisions/2026-11-03-trinity-directdraw-and-flips.md)):
a fourth `cedar_hw16.c` row and `V9X_DD_ENGINE_CAP_EG_TRINITY` (Michael),
32-bit BARs accepted, fill/copy pass and 69 of 69 flips are taken by the
chip. No Direct3D, so Quake 3 is expected not to start on this build
(untried). Phase 4 done (2026-11-03,
[record](../decisions/2026-11-03-trinity-cp-microcode.md)): the CP runs
`ARUBA_pfp/me.bin` (2,176 dwords each) through Cayman's three-ring
bring-up, ring test three of three, **without the RLC microcode**.
Phase 5 done (2026-11-03, `bd3253a2` to `7517eac2`): the Cayman module,
`cayman_gpu_init` as a table and V9XCM3D's textured copy byte-exact six
of six. Phase 6 done (2026-11-04,
[record](../decisions/2026-11-04-trinity-direct3d-opengl-gates.md)):
V9XD32 passes at 32 and 16 bpp once the relocator's end-of-program bug
was fixed ([record](../decisions/2026-11-03-trinity-hal-direct3d-hang.md)),
V9XDDP completes, 3DMark 2000 completes and the Quake 3 demo runs with
every batch on the engine. The same build passes on the HD 7470 (now in
M-BXMFA8). Not done: Gouraud, Z16 and modulate as separate V9XCM3D
probes (V9XD32 covers them through the HAL), and the zero flip counts.

## Why now

The issue #12 submitter, who owns M-D7TFW4 and M-BXMFA8, asked in
remote-help chat msg 87 (2026-11-02 15:30Z) for "the Radeon HD 7660D that
is integrated on this CPU". Michael chose it over finishing the HD 7470's
Phase 6 (2026-11-03). The HD 7470 is no longer in the machine (removed or
disabled in the BIOS, not asked), so the Caicos code has no tester until
it comes back.

## What Linux says Trinity is

Linux calls it `CHIP_ARUBA` (`radeon_family.h:90`, after `CHIP_CAYMAN`)
and drives it with Cayman's code in `ni.c` and the `trinity_asic` table
(`radeon_asic.c:1778`). Sources are the `torvalds/linux` copy in
`build\external\evergreen\linux-radeon\`, the same one the Evergreen plans
cite.

| | Evergreen engine as shipped (Cedar, Caicos, Juniper) | Trinity `9901` | Source |
|---|---|---|---|
| Shader core | VLIW5 | **VLIW4** (Cayman's) | Linux family; encoding differences to read in Mesa's r600 `CAYMAN` paths |
| SE / tile pipes / SIMDs / backends | 1 / 2 / 2 / 1 (Cedar, Caicos) | 1 / 2 / **6** / **2** (fuses agree) | `ni.c:902-972` |
| GPRs / threads / stack | 256 / 192 / 256 | 256 / 256 / 512, 4 pipes per SIMD, 8 SX sets, export sizes 256 / 64 / 192 | `ni.c:960-972` |
| Golden `GB_ADDR_CONFIG` | `02010001` | **`12010001`** | `nid.h:45` |
| Backend map | `GB_BACKEND_MAP` 0 | built by `r6xx_remap_render_backend` from the disable mask (two backends) | `ni.c:1113` |
| Banks | from `MC_ARB_RAMCFG` | eight, hard-coded on Fusion | `ni.c:1043` |
| CP microcode | `CEDAR`/`CAICOS`/`JUNIPER` pfp + me, 1120 + 1376 dwords | **`ARUBA_pfp/me.bin`, 2176 + 2176 dwords** | `ni.c:734`, `radeon_ucode.h:33-34` |
| RLC microcode | none loaded | **`ARUBA_rlc.bin`, 1536 dwords, loaded on every IGP**, with power gating | `radeon_ucode.h:50`, `ni.c:2163`, `evergreen.c:4379-4410` |
| MC microcode | none (Caicos's DDR3 does not need it) | none: Linux skips it on `RADEON_IS_IGP` | `ni.c:2149` |
| CP rings | one | three (`CP_RB0/1/2`); Linux uses RB0 for gfx | `ni.c:1598-1640` |
| `ME_INITIALIZE` | Evergreen's | `max_hw_contexts - 1` (8 contexts on `9901`), then `cayman_default_state` (not yet fetched) | `ni.c:1524-1585` |
| DMA engines | one, Evergreen packets | two (DMA1 at +800h); `trinity_asic` copies with `evergreen_copy_dma` | `nid.h:1302`, `radeon_asic.c:1828` |
| Display | DCE 4 / 5 | DCE 6.1 (`dce6_bandwidth_update`); flips by `evergreen_page_flip` | `radeon_asic.c:1819`, `:1881` |
| VRAM | the card's | the 256 MiB system-memory carve-out, at GPU `F_00000000` | probe |

What this means for the tree:

- **DMA 2D and flips should carry over.** Same DMA packets, a GPU address
  above 4 GiB as Caicos had, and D1 at Evergreen's offsets (the probe's D1
  reads match the VBE mode). These are the cheap phases.
- **The CP needs new microcode and probably the RLC.** The images are
  Cayman-sized, so `eg_cp.c`'s loader lengths become per chip. Whether the
  CP runs ring tests without `ARUBA_rlc.bin` is the first thing to measure.
  Linux never tries. With the RLC off, power gating stays off (`RLC_CNTL`
  reads 0 now), which may be the simpler state.
- **3D is a new shader encoder.** Every shader `eg_shader.c` emits is VLIW5
  ALU groups. VLIW4 has no T slot and Cayman changed CF instructions
  (Mesa's r600 compiler has a `CAYMAN` class for this). The host-test
  pattern holds: rebuild Linux's Cayman blit shaders
  (`cayman_blit_shaders.c`, not yet fetched) word for word first.
- **GPU init** becomes a Cayman table: golden address config, a real
  backend map, Cayman's `SQ_*` resource registers. This is the
  chip-config-struct idea in the Juniper/Caicos plan, now with a second
  register layout.

## Layering

One `ati` family (`one-family-per-vendor`). The question is where Trinity
sits inside it:

- **A row in `cedar_hw16.c` and a chip bit on `ATI_EVERGREEN`**, as Caicos
  and Juniper are. That works for DMA and flips, which are Evergreen's.
- **A Cayman engine beside Evergreen** (`src\chipsets\ati\cayman\` for the
  CP, GPU init and VLIW4 shaders), sharing `eg_dma.c` and `eg_scanout.c`.
  The 3D side differs in its shader ISA and init tables, not just in data.

Recommended: the row in `cedar_hw16.c` for phases 2-3. Decide the 3D shape
before Phase 4 (decision 1).

**Decided (Michael, 2026-11-03): a separate Cayman module.** The shape:

- `src\chipsets\ati\cayman\` with `include\velocity9x\cm_*.h`, pure policy
  and host-tested, as `evergreen\` is:
  - `cm_shader.c`: the Cayman (VLIW4) encoder. It calls `eg_shader.c`
    for the words the two ISAs share (ALU slots, texture fetch, export
    fields) and owns what differs: `CF_END` instead of the end-of-program
    bit, vertex fetches in TC clauses (Cayman has no VC, Mesa
    `r600_isa.c:399`), no mega-fetch fields, and later the VLIW4 ALU
    groups (no T slot). First test: Linux 3.9's `cayman_vs` and
    `cayman_ps` rebuilt word for word.
  - `cm_3d.c`: `cayman_gpu_init`'s writes for `9901` as a table, the
    clear state (`cayman_default_state`) and Cayman's packet differences.
- The DMA engine, scanout, heap and engine hook stay shared
  (`eg_dma.c`, `eg_scanout.c`, `cedar_hw16.c`'s row).
- The HAL side (`eg_cp.c`, `d3d_eg.c`) chooses Cayman's tables by
  `V9X_DD_ENGINE_CAP_EG_TRINITY`. Whether that becomes a `cm_cp.c` beside
  `eg_cp.c` or stays one CP module calling two policy modules is decided
  when Phase 5 reaches the HAL.

Phase 5 progress (2026-11-03, host only, nothing on the machine yet):

- `cm_shader.c` rebuilds Linux 3.9's `cayman_vs` and `cayman_ps` word for
  word (`bd3253a2`).
- `cm_3d.c` holds `cayman_gpu_init`'s 41 writes for `9901` (golden
  `GB_ADDR_CONFIG` `12010001`, backend map `10h`, two texture caches, no
  SQ thread, stack or GPR splits, dynamic GPRs, the six TMP ring bases
  zeroed, `SQ_CONFIG` `43h`). Its `r6xx_remap_render_backend` port also
  reproduces Juniper's `3210` and Cedar's `0`, the maps that drew. Not
  in the table: `CGTS_SM_CTRL_REG`'s sixteen `OVERRIDE` writes and
  restore, which V9XEG3D must do in its op list.
- `cm_3d.c` also holds the Cayman clear state and stream differences
  (`053405e0`): `cayman_default_state`, `ME_INITIALIZE` with eight
  contexts, `set_default_state` without the SQ split block, and
  `CP_COHER_CNTL2` before each ring `SURFACE_SYNC`. The 2x1 scissor floor
  is still to add, when a draw can be that small.
- **First draw on the chip** (2026-11-03,
  [record](../decisions/2026-11-03-trinity-first-3d-draw.md)): V9XCM3D,
  the blit on Cayman's path with 40-bit addresses, byte-exact six of six,
  linear and 1D tiled. Next: the Direct3D shaders in VLIW4, then the HAL.

## Phases

Each ends in a decision record. Same order as Caicos.

0. **Survey, tier 0.** Done.
1. **Read-only register probe.** Done (V9XEGMM accepts `9901`).
2. **Identity and DMA 2D.** V9XEGDMA's write probe on `9901` (fills,
   copies, overlap, soft reset per session, 40-bit addresses), DMA0 only.
   Then claim `1002:9901` in the `ati` manifest, `v9x_ati_device_ids[]`,
   `backend_registry_table.inc` and a `cedar_hw16.c` row (fill and copy,
   CP refused). V9XDDP and V9XMSW on the `ati` build at 16 and 32 bpp. The
   heap is the carve-out less what VBE uses; check the mini-VDD's
   `CONFIG_MEMSIZE` floor (256 MiB, exactly this chip's value) and the
   BAR0 + 240 MiB engine window against a 256 MiB BAR0.
3. **Flips** through D1, as on Caicos. Watch whether Quake 3 stops starting
   on an engine claimed without D3D, as it did on Caicos (Phase 3 there).
   If it does, decide whether to ship phase 2 alone.
4. **CP.** Fetch `ARUBA_pfp/me/rlc.bin` from the gitlab.com
   `kernel-firmware` mirror, pin them in `gen-evergreen-microcode.ps1`.
   Fetch `cayman_blit_shaders.c` for `cayman_default_state`. V9XEGCP with
   Cayman loader lengths: ring test three of three without the RLC first,
   then with it if that fails.
5. **GPU init and VLIW4 shaders.** Host test first: the Cayman init table
   and the encoder against Linux's Cayman blit shaders word for word. Then
   V9XEG3D's textured copy, Gouraud, Z16 and modulate, byte-exact.
6. **Direct3D and OpenGL gates**: V9XD32 at 16 and 32 bpp, V9XDDP,
   3DMark 2000, Quake 3 demo with every batch on the engine.

## Decisions for Michael

1. Trinity's 3D: a Cayman engine module beside Evergreen's, or more chip
   bits on `ATI_EVERGREEN` (before Phase 4). **Decided:** a separate
   Cayman module (2026-11-03).
2. Shipping `ARUBA_rlc.bin` (6 KiB) as well as the CP images, if the CP
   needs it. **Moot:** the CP runs without the RLC.
3. Whether to ask the owner where the HD 7470 went. **Answered by the
   machines:** it is in M-BXMFA8 (found at boot 60, 2026-11-04; the
   owner's msg 83 of 2026-11-01 already calls it "newly added"), in place
   of the HD 5770, so Caicos has a tester again and Juniper does not.

## Owed replies in remote-help chat

- M-D7TFW4: msg 82 (Quake 3 input complaint, 2026-10-31) and msg 87.
- M-BXMFA8: msg 83 (the HD 7470's 3DMark 2000 score, 2026-11-01).

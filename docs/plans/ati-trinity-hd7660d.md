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
BIOS, unnamed in Linux) kept as found. The driver claim is next.

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
   bits on `ATI_EVERGREEN` (before Phase 4).
2. Shipping `ARUBA_rlc.bin` (6 KiB) as well as the CP images, if the CP
   needs it. The licence is the same `LICENSE.radeon` already in the tree.
3. Whether to ask the owner where the HD 7470 went, and whether it can go
   back in a slot for Caicos regression runs while the APU is primary.

## Owed replies in remote-help chat

- M-D7TFW4: msg 82 (Quake 3 input complaint, 2026-10-31) and msg 87.

# Radeon HD 5770 (Juniper) and HD 7470 (Caicos) on the Evergreen engine

Date: 2026-10-30

Status: Phase 0 done (2026-10-31,
[record](../decisions/2026-10-31-caicos-juniper-tier0-survey.md)). Both
owners had already installed 0.33.0 `vbe`, and both cards reach
enable-ok. Ids measured: Caicos `1002:6778` (M-D7TFW4), Juniper
`1002:68B8` (M-BXMFA8). Caicos passes the GDI, mode and DirectDraw tests.
Phase 1 read on Caicos (2026-10-31,
[record](../decisions/2026-10-31-caicos-register-probe.md)): 2 GiB DDR3,
so no MC microcode; one backend, two SIMDs; VRAM at GPU `F_00000000`,
which the shipped Evergreen engine refuses (`v9x_eg_fb_start`), so
Phase 2 starts with 40-bit GPU addresses. Phase 2 done (2026-10-31,
[record](../decisions/2026-10-31-caicos-dma-write-probe.md)): DMA packets
and the driver carry bits 39:32; Caicos is a row in `cedar_hw16.c`
(decision 1, Michael) claiming fill and copy, V9XDDP's blit checks pass on
the engine at 16 bpp. Phase 3 (2026-10-31,
[record](../decisions/2026-10-31-caicos-page-flips.md)): flips handled
and confirmed by the chip, vblank pacing unmeasured. **On the ati build
Quake 3 does not start** (no software GL once an engine is claimed
without D3D); it runs only on vbe until Phase 5. Parent plans:
[ati-evergreen-cedar.md](ati-evergreen-cedar.md) (2D, flips, heap) and
[ati-evergreen-cedar-3d.md](ati-evergreen-cedar-3d.md) (CP, Direct3D,
OpenGL).

## Why now

Both requests came through remote-help chat from the issue #12 submitter,
who owns both machines:

| Machine | Computer | Card (owner's words) | Asked |
|---|---|---|---|
| `M-BXMFA8` | W9QI3632 | Radeon HD 5770, "another Terascale 2" | 2026-10-29 14:16Z, msg 81 |
| `M-D7TFW4` | W9QI6DFC | Radeon HD 7470 (Caicos XT) | 2026-10-29 12:43Z, msg 79; "go ahead when you're available" |

Both are Win98 SE, agent 0.13.1, remote help on, online on 2026-10-30.
`M-D7TFW4` registered 2026-10-29; its first commands were Phase 0's.

**M-BXMFA8 no longer has the HD 5450.** The Cedar that every Evergreen
record was measured on has been swapped out. Until the Cedar comes back, no machine can regression-test the shipped
Cedar code.

PCI ids, read from the registry in Phase 0: Juniper XT (HD 5770)
`1002:68B8` subsystem `1002:2543`, Caicos XT (HD 7470) `1002:6778`
subsystem `174B:E145`. Owner had already put 0.33.0 `vbe` on M-BXMFA8,
so it is not stuck at a new-hardware prompt.

## What the sources say differs from Cedar

From `build\external\evergreen\linux-radeon\` (the same `torvalds/linux`
copy the Cedar plans cite). `ni.c`, which loads BTC (Barts/Turks/Caicos)
microcode, is not in that folder yet.

| | Cedar (shipped) | Juniper | Caicos | Source |
|---|---|---|---|---|
| Linux family | Evergreen, DCE 4 | Evergreen, DCE 4 | BTC ("Northern Islands" VLIW5), DCE 5 | `radeon_family.h` |
| Shader ISA | Evergreen VLIW5 | same | same (Cayman's VLIW4 is the different one) | — |
| SEs / pipes / tile pipes | 1 / 2 / 2 | 1 / 4 / 4 | 1 / 2 / 2 | `evergreen.c:3182`, `:3365` |
| SIMDs / backends | 2 / 1 | 10 / 4 | 2 / 1 | same |
| GPRs / threads / stack | 256 / 192 / 256 | 256 / 248 / 512 | 256 / 192 / 256 | same |
| SX export sizes | 128 / 32 / 96 | 256 / 64 / ? | 128 / 32 / ? | same, to finish reading |
| `GB_ADDR_CONFIG` golden | `02010001` | `02010002` | `02010001` | `evergreend.h:43-47` |
| Vertex cache | none (`VC_ENABLE` cleared, `TC_ONLY`) | yes (`VC_AND_TC`) | none | `evergreen.c:3591`, `:3655` |
| Golden registers | `cedar_*` | `evergreen_golden_registers{,2}` + `juniper_mgcg_init` | `caicos_golden_registers` | `evergreen.c:1012`, `:1073` |
| CP microcode | `CEDAR_pfp/me.bin`, 1120 + 1376 dwords | `JUNIPER_pfp/me.bin`, same sizes | `CAICOS_pfp/me.bin`, sizes per `ni.c` (unread) | `r600.c:2497-2560` |
| MC microcode | none | none | `CAICOS_mc.bin` (`BTC_MC_UCODE_SIZE` 6024) when the MC sequencer is not running | `evergreen.c:5025` |

Consequences for the tree:

- **Caicos is Cedar's shape** (1 SE, 2 pipes, 2 SIMDs, 1 backend, same
  golden `GB_ADDR_CONFIG`, no vertex cache). The deltas are its microcode,
  DCE 5, its golden registers and the MC-microcode question. That makes it
  the cheaper port, and the closest stand-in for Cedar regression while the
  HD 5450 is out.
- **Juniper has four backends and four tile pipes.** `eg_3d.c`'s
  `v9x_eg_cedar_gpu_init[]` is Cedar's numbers as literals, including
  `GB_BACKEND_MAP` 0 and the SQ resource splits. Juniper needs the backend
  map built from the RCU fuse disable mask, as `evergreen_gpu_init` does;
  a wrong map is the kind of fault that drew a quarter of the pixels in the
  wrong place on Cedar's linear colour buffer. The vertex cache changes
  `SQ_CONFIG` and `VGT_CACHE_INVALIDATION`.
- Surface layouts (1D-tiled colour, linear-aligned with
  `NON_DISP_TILING_ORDER`, Z16 rows on 32 bytes) were measured on 2 tile
  pipes. Whether 4 pipes change any of them is unmeasured; V9XD32 and
  V9XDDP decide it.
- **MC microcode on Caicos.** Linux loads it only when the sequencer is not
  already running; after the VBE BIOS has POSTed the card it should be. Read
  `MC_SEQ_SUP_CNTL` before planning anything else. If it is not running the
  plan stops for a decision, since that is a second firmware blob and a
  memory-controller write.
- **Microcode size on the floppy set.** Disk 1 was already 3,919 bytes over
  when Cedar's CP microcode joined (the trace tool moved to disk 3). Each
  extra chip adds about 9.8 KiB of PFP+ME. First check whether
  `JUNIPER_*.bin` or `CAICOS_*.bin` are byte-identical to `CEDAR_*.bin`;
  if they are, nothing new ships.

## Layering

One `ati` family, one `ATI_EVERGREEN` engine type
(`one-family-per-vendor`). Per-chip differences become data:

- `src\chipsets\ati\evergreen\`: a `juniper_hw16.c` and `caicos_hw16.c`
  beside `cedar_hw16.c`, or one Evergreen hw16 with a per-chip row. Either
  way the engine hook, the 128 MiB heap and the diagnostics stay shared.
  Adding external symbols is a design change; decide which shape before
  coding.
- A chip-config struct (pipes, backends, SIMDs, threads, stack, SX sets and
  sizes, vertex cache, golden `GB_ADDR_CONFIG`) and a host-tested
  function that turns it into the `gpu_init` update list. Its first test is
  that Cedar's config reproduces `v9x_eg_cedar_gpu_init[]` word for word.
- The microcode chosen by chip id in `eg_cp.c`; the 16-bit side passes the
  chip down through the engine descriptor, as it does for the VI path.
- Scanout stays in `eg_scanout.c` if DCE 5's `GRPH_*` offsets match DCE 4
  (Linux drives both with `evergreen_page_flip`; to confirm on the card).

## Phases

Same order as the Cedar bring-up. Each phase ends in a decision record.

0. **Survey, tier-0** (both machines). Read the display devnode ids,
   subsystem, revision and audio function from the registry; record what
   M-BXMFA8 is running after the swap. Install the 0.33.0 `vbe` package,
   `reset`, and record V9XHW.INI, modes and GDI tests. Ask each owner
   whether the monitor shows a clean picture.
1. **Read-only register probe** (V9XEGMM, built for Cedar). BAR0/2/4 with
   high dwords, `CONFIG_MEMSIZE`, `MC_VM_FB_LOCATION`, `GB_ADDR_CONFIG`,
   `CP_ME_CNTL`, `SRBM_STATUS`, `DMA_*`, each CRTC's `GRPH_*` and
   `CRTC_STATUS`, the RCU backend-disable fuses (`0x203`/`0x204`), and on
   Caicos `MC_SEQ_SUP_CNTL`. Check the mini-VDD's 256 MiB `CONFIG_MEMSIZE`
   floor and the BAR0 + 240 MiB engine window against each card's BAR0.
2. **Identity and DMA 2D.** Claim the measured ids in the `ati` manifest,
   `v9x_ati_device_ids[]` and `backend_registry_table.inc`. Run V9XEGDMA's
   write probe (fills, copies, overlapping copies, soft reset per session),
   then the HAL's fill and copy at 16 and 32 bpp across mode switches.
3. **Flips** through D1's surface address; 3DMark 2000 watched by the owner
   for the every-second-frame flicker Cedar had before flips.
4. **CP.** Fetch and compare the `JUNIPER_*`/`CAICOS_*` microcode (and
   `ni.c`); extend `gen-evergreen-microcode.ps1`; V9XEGCP's ring test,
   three runs of three.
5. **GPU init from the chip config**, host test first, then V9XEG3D's
   textured copy on 1D-tiled and linear-aligned surfaces, Gouraud, Z16 and
   modulate draws, byte-exact.
6. **Direct3D and OpenGL gates**: V9XD32 at 16 and 32 bpp, V9XDDP (fails
   must stay a subset of Cedar's), 3DMark 2000 default, 3DMark 2001 SE,
   Quake 3 demo timedemo with every batch on the engine.

Recommended order: Caicos (M-D7TFW4) through phase 6 first, since only its
microcode, DCE 5 and MC differ from Cedar; then Juniper, where the
remaining change is backends, pipes and the vertex cache.

## Open Cedar defects to carry over

Reported by the owner on M-BXMFA8 (chat msgs 57-78 and `Issues.txt` on its
desktop, which has not been copied into `docs\probe\`), none fixed in
0.33.0. They are probably engine-generic, so re-check each on the first
card that passes phase 6:

- **3DMark 2000 Helicopter: trees vanish and reappear**, even near the
  helicopter. The owner found it happens only with 3DMark's CPU
  optimisation "Intel Pentium III", not "D3D Software T&L" (msg 78,
  2026-10-29). Not reproduced or explained here. A V9XTRACE of both
  settings on the same frame would show which DrawPrimitive path or vertex
  data differs.
- **Quake 3: corruption after an in-game resolution change** (640x480 to
  800x600), only with a 32 bpp desktop; restarting the game clears it.
- **Quake 3: 1-2 fps at 1280x1024 and above**, while 3DMark 2000 runs fine
  at 1600x1200x32. Unmeasured; a first check is whether the GL path left
  the engine or the heap overflowed.
- **3DMark 2001 SE: plants**, and the 32 bpp resolution list (the latter
  addressed by the DD mode-subset fix,
  [record](../decisions/2026-10-29-cedar-dd-mode-subset.md); the owner has
  not confirmed).

## Decisions for Michael

1. Juniper and Caicos as separate `*_hw16.c` modules or one Evergreen
   hw16 with per-chip rows.
2. If either chip's microcode differs from Cedar's: where the extra
   ~9.8 KiB per chip goes on the floppy set.
3. If Caicos's MC sequencer is not running after POST: whether to ship and
   load `CAICOS_mc.bin`, or leave Caicos at 2D and flips.
4. Whether to ask the owner to put the HD 5450 back in a machine for
   Cedar regression runs, since none is available now.

## Owed replies in remote-help chat

- M-BXMFA8: msgs 76-78 (tree issue follow-up, screen-recording offer, the
  Pentium III finding) and msg 81 (HD 5770) have no reply.
- M-D7TFW4: msgs 79-80 have no reply.

# Radeon HD 5450 (Evergreen "Cedar") in the ati family

Date: 2026-10-26

Status: Phase 0 done (tier-0 `vbe` on M-BXMFA8,
[record](../decisions/2026-10-26-m-bxmfa8-radeon-68f9-first-look.md)).
Phase 1 coded and gated, not installed. Phase 2 read on the card
([record](../decisions/2026-10-26-cedar-register-probe.md)): 1 GiB of
VRAM at GPU address 0, the VBE scanout at GPU 0, the DMA engine idle and
out of reset, the CP halted. Phase 3's write probe passed
([record](../decisions/2026-10-26-cedar-dma-write-probe.md)): dword and
byte copies, overlapping copies (memmove) and constant fills, byte-exact,
five runs in a row once each ring session starts and ends with a DMA soft
reset. Register facts in the table below come from Linux `radeon` source;
the records say which ones the card has confirmed.

Target: Cedar (`1002:68F9`, HD 5450 class) as a fifth chip generation in
the one `ati` family (`one-family-per-vendor`), with its own module tree
`src/chipsets/ati/evergreen/` and its own engine type. It shares nothing
with the Rage 128 below PCI/VGA. Test machine: `M-BXMFA8`, reached only
through the remote-help relay. Its owner also has a Juniper HD 5770 and
a Caicos HD 7470 (issue #12), so the same code can be checked on a
second and third die later.

## Sources

Collected 2026-10-26 into `build\external\evergreen\linux-radeon\`
(outside version control), from `torvalds/linux` master at `6b4b543f`,
`drivers/gpu/drm/radeon/`: `evergreend.h`, `evergreen_reg.h`,
`evergreen.c`, `evergreen_dma.c`, `evergreen_cs.c`, `r600_dma.c`,
`r600d.h`, `r600.c`, `rv770.c`, `radeon_ucode.h`, `radeon_asic.c`,
`radeon_family.h`, `atombios.h`, `atombios_crtc.c`. Citations below are
`file:line` in that folder.

Published by AMD, not yet downloaded:

- *Evergreen Family Instruction Set Architecture*, rev 1.1a, November
  2011 (the shader ISA), on amd.com.
- *Radeon Evergreen 3D Register Reference Guide*, rev 1.0, April 2011,
  on docs.amd.com.
- The *R6xx/R7xx 3D Register Reference* and the *Cayman 3D register*
  guide on x.org, close relatives.

AMD did not publish a DCE4 display or memory-controller register guide
that this search found; for those, Linux is the reference.

## What the sources settle

| | Evergreen (Cedar) per Linux | Consequence |
|---|---|---|
| BAR0 | framebuffer, 64-bit prefetchable (256 MiB on M-BXMFA8) | LFB; VBE already gives `D0000000` |
| BAR2 | registers, 64-bit, 128 KiB | A new BAR read at 18h **and 1Ch**, refusing a base above 4 GiB (the Intel 64-bit pattern); a new mini-VDD map. `V9xPciReadAtiMmioBar` reads 18h only and would accept half a 64-bit BAR |
| BAR4 | 256-byte I/O | Probably `MM_INDEX`/`MM_DATA` at +0/+4 (Radeon convention; not in these headers, unverified). Would let a Win32 probe read registers by port I/O with no map |
| VRAM size | `CONFIG_MEMSIZE` 0x5428, in MiB on Evergreen (`evergreend.h:446`, `evergreen.c:3714`) | A `read_video_memory` hook. VBE says 16 MiB; the card has more |
| GPU address of VRAM | `MC_VM_FB_LOCATION` 0x2024: bits 15:0 start >> 24, 31:16 end >> 24 (`evergreen.c:2853`) | Every engine and scanout address is MC-relative; read it, never reprogram it (the BIOS has POSTed the card) |
| CPU/GPU coherence | `HDP_MEM_COHERENCY_FLUSH_CNTL` 0x5480 = 1 (`evergreend.h:919`) | Flush after CPU writes to VRAM before the GPU reads them (ring contents, source surfaces) |
| 2D engine | **None.** R600 and later dropped the fixed 2D engine; Linux EXA draws 2D with the 3D engine and shaders | No `DP_*`-style path exists to port |
| Async DMA engine | Ring at 0xD000-0xD0BC, started by register writes alone; **no microcode** (`r600_dma.c:119`) | The 2D path for phase 3 |
| DMA copy | `DMA_PACKET(COPY, 0x00, n)`: dword-aligned linear, 5 dwords; `0x40` byte-aligned linear (`evergreen_cs.c:3019`); rectangular "L2L partial" is Cayman only (`evergreen_cs.c:3045`) | A rectangle is one packet per scanline |
| DMA fill | `DMA_PACKET(CONSTANT_FILL, 0, n)`: dst low, value, dst high in bits 23:16 (`evergreen_cs.c:3284`); dword granular | Fills of dword-aligned spans; the CPU or a byte copy does ragged edges at 8/16 bpp |
| DMA idle | `DMA_STATUS_REG` 0xD034 bit 0 | `wait_idle` |
| Command processor (CP) | Needs PFP (1120 dwords) and ME (1376 dwords) microcode, plus RLC (768) (`radeon_ucode.h:31-48`, `evergreen.c:2968`) | Required for 3D, CP DMA and anything in the shader core. Linux's `CEDAR_*.bin` from linux-firmware; redistribution terms to check before shipping |
| Scanout / flips | `GRPH_PRIMARY_SURFACE_ADDRESS` 0x6810 (+`_HIGH` 0x681C), latched at vblank unless `GRPH_FLIP_CONTROL` bit 0; `GRPH_UPDATE` 0x6844 bit 2 = pending (`evergreen.c:1416-1450`) | `set_display_start` and `flip_pending` hooks; addresses are MC addresses |
| Vblank | `CRTC_STATUS` 0x6E8C bit 0, per-CRTC offsets 0x6DF0 base (`evergreen_reg.h:224-241`) | `in_vblank` |
| Cursor | `CUR_CONTROL` 0x6998 to `CUR_UPDATE` 0x69B8, surface in VRAM, 4 KiB aligned | First hardware cursor in the tree; optional |
| Palette | `DC_LUT_*` 0x69E0-0x6A18 | `load_gamma` |
| Mode setting | AtomBIOS command tables (`atombios_crtc.c`); VBE already drives it | Keep `vbe-lfb`. Native mode setting is out of scope, and so is 1920x1200 unless the BIOS lists it |
| Cedar shader config | 1 SE, 2 SIMDs, 2 pipes, 1 backend, 256 GPRs (`evergreen.c:3226`), `GB_ADDR_CONFIG` golden 0x02010001 | Matters only for 3D |

## The design consequence

Every earlier chip in the tree had a fixed-function 2D engine, and its
3D used state registers. Evergreen has neither. So:

- **2D acceleration** comes from the DMA engine: VRAM-to-VRAM copies and
  constant fills, no microcode, no shaders. It cannot do ROPs other than
  SRCCOPY, colour keys, or stretching; those stay on the CPU, which is
  where the vbe tier-0 already does them.
- **Host-to-screen** DMA needs the engine to reach system memory. That
  means the PCIe GART (`VM_CONTEXT0_*`, `VM_L2_CNTL` 0x1400) or, maybe,
  the MC's linear "AGP" aperture (`MC_VM_AGP_BASE/TOP/BOT`, which Linux
  disables on PCIe). The second is a hypothesis to test, not a fact.
- **Flips and vblank** are plain DCE4 register writes and should be the
  cheapest real win. Issue #16's fullscreen flicker on a Redwood under
  `vbe` is the obvious thing to measure them against.
- **Direct3D** needs the CP running with AMD's microcode, then vertex and
  pixel shaders written in the Evergreen ISA for each fixed-function
  state, much as the Haswell T&L plan does with VS kernels. That is a
  separate plan, after 2D and flips are proven.

## Phases

Same order as `ati-rage128.md` and the Haswell bring-up.

0. **Baseline (done).** Tier-0 `vbe` on M-BXMFA8: enable-ok, 39 modes,
   GDI and mode cycles pass. Still owed: the owner's view of the
   monitor, and a fullscreen game watched for the issue #16 flicker.
1. **Identity.** Claim `1002:68F9` exactly in the `ati` manifest and
   `v9x_ati_device_ids[]` as a tier-0 device with no hooks (an
   `evergreen_hw16.c` with `fill_engine_descriptor` NULL). The INF then
   names the card. Other Cedar ids are not claimed until a survey of one
   is in hand.
2. **Read-only register probe**, a Win32 tool like the Rage 128 one.
   Read: BAR0/BAR2/BAR4 including the high dwords, `CONFIG_MEMSIZE`,
   `MC_VM_FB_LOCATION`, `MC_ARB_RAMCFG`, `GRBM_STATUS`, `SRBM_STATUS`,
   `SRBM_SOFT_RESET` (is the DMA block held in reset after POST?),
   `DMA_RB_CNTL`/`DMA_STATUS_REG`, and every CRTC's `GRPH_*` and
   `CRTC_STATUS` at each VBE mode. Prove the MC address of the VBE
   scanout equals `FB_LOCATION` start + LFB offset. Try BAR4 `MM_INDEX`
   reads against the BAR2 values. Then the `read_video_memory` hook, the
   64-bit-aware BAR read and the mini-VDD map.
3. **DMA 2D.** A write probe first: a ring in VRAM, HDP flush, one
   `CONSTANT_FILL`, one `COPY`, fence by `DMA_PACKET_FENCE` to VRAM, then
   byte-exact readback at 8, 16 and 32 bpp. Measure what an overlapping
   copy does (forward only?), since screen-to-screen scrolls overlap.
   Then the engine32 ops (`fill`, `copy`, `wait_idle`) under a new
   `V9X_DD_ENGINE_TYPE_*`, pure packet builders host-tested first.
4. **Flips and vblank** via `GRPH_PRIMARY_SURFACE_ADDRESS` and
   `CRTC_STATUS`, a new `*_scanout.c` and its branch in the scanout
   chain. Re-run the issue #16 games.
5. **Heap beyond 16 MiB**, following the Haswell precedent, once
   `CONFIG_MEMSIZE` is measured.
6. **3D**: its own plan, with the microcode licensing question settled
   first.

## Unknowns that need a measurement

- Whether the DMA engine is usable as POSTed by the BIOS, or needs
  `SRBM_SOFT_RESET` and the semaphore/timer setup Linux does.
- Whether a VBE mode set (AtomBIOS) disturbs the DMA ring or the MC.
- What overlapping DMA copies do.
- Whether the DOS box and the return from it leave the ring and scanout
  state alone. Issue #16 reports DOS-return problems on a Redwood under
  `vbe` already, so this needs a baseline before any change.
- The true VRAM size and whether the 256 MiB BAR covers all of it.

## Decisions for Michael

1. Whether 2D through the DMA engine (copies and fills only) is worth a
   family member, given that GDI on the CPU into an uncached LFB is
   already what `vbe` does. A measurement of the uncached LFB write
   speed against a DMA fill would answer it.
2. Whether to plan 3D at all: it needs AMD's CP microcode shipped with
   the driver and a shader compiler for fixed-function state.

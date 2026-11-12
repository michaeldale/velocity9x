# AMD Oland (Radeon R7 240, GCN 1) in the ati family

Date: 2026-11-12

Status: Proposed. Nothing coded, nothing measured. No machine with an
R7 240 is on record, so Phase 0 waits on one.

Target: the Radeon R7 240, an Oland part, which Linux binds as
`CHIP_OLAND` (Southern Islands, `si.c` in radeon, `si.c`/`gfx_v6_0.c` in
amdgpu): GFX6, the SI async DMA engine, DCE 6.4. The retail card is
usually `1002:6613`; OEM boards use other ids in the Oland range
(`6600`-`663F` in Linux's table). The id is read from the machine, not
taken from this paragraph.

This is a discrete PCIe card with its own VRAM, the first GCN part in the
family that is not an APU carve-out.

## What carries over, and what does not

Read from the tree on 2026-11-12, not run on an Oland:

| Piece | Oland | Nearest code in the tree |
|---|---|---|
| Display | DCE 6.4; radeon's `si.c` uses Evergreen's CRTC offsets | `eg_scanout.c`, as Mullins' DCE 8 does |
| 2D engine | SI async DMA at `0xD000`, Cayman-style packets, no microcode | `eg_dma.c` / `eng_eg.c` (Cedar, Caicos, Trinity) |
| Graphics CP | PFP, ME, CE and RLC microcode; no MEC | `vi_cp.c` in its Mullins mode |
| gpu_init | Oland's own tile-mode table, address and raster config | `cik_gpu_init.c` as the shape; values from `si.c` |
| Shader ISA | GCN 1 (Southern Islands) | `cik_shader.c` (GCN 2) |
| HDP flush through BAR0 | Required | Already done by `eng_eg.c` and `vi_cp.c` |
| Register BAR | BAR2 | Mullins and Carrizo read BAR5 (`V9xPciReadViBars`) |

Two things here are expectations from the Linux sources, not facts about
the chip: that the SI DMA packets differ from Evergreen's only where
`si_dma.c` says so (it counts a linear copy in bytes where
`evergreen_dma.c` counts dwords), and that the GFX6 encodings of the
instructions `cik_shader.c` emits are unchanged. Each is checked by the
phase that first depends on it.

## The decision to take before Phase 3

DirectDraw and Direct3D are selected by engine type
(`d3d_select.c`: `V9X_DD_ENGINE_TYPE_ATI_VI` reaches the GCN Direct3D).
Oland's 2D engine is the Evergreen kind and its 3D is the GCN kind, a
pairing no row has had. Two routes:

- **A. Teach `eng_vi.c` the SI DMA engine.** One engine type, the VI
  one, with an Oland identity bit; `vi_cp.c` and `d3d_vi.c` unchanged in
  how they find each other. The DMA packet builder is `eg_dma.c`'s.
- **B. Run DirectDraw on `eng_eg.c` and add a GCN 3D path to the EG
  engine type.** Reuses the DMA engine as it is, but splits GCN 3D across
  two engine types.

A looks smaller and keeps all GCN 3D behind one engine type. Michael
decides; this plan assumes A until he does. Either needs a new
`V9X_DD_ENGINE_CAP_*` bit (`0x20000000` is the next free one after
`V9X_DD_ENGINE_CAP_VI_MULLINS`).

## Sources

To add to `build\external\carrizo\linux\` (outside version control) by
sparse checkout of the same commit: `amdgpu/{si,si_dma,gfx_v6_0,gmc_v6_0,
dce_v6_0,si_ih}.c`, `sid.h`, `si_enums.h`, and radeon's `si.c`, `sid.h`,
`si_dma.c` and `si_reg.h` for the older reading of the same registers.
Microcode in `build\external\oland\firmware\`: linux-firmware's
`radeon/oland_{pfp,me,ce,rlc}.bin` (the new-format files with a header;
the licence to be checked against `LICENSE.radeon`, which is not the
amdgpu file Carrizo and Mullins carry). AMD's *Southern Islands Series
Instruction Set Architecture* reference for Phase 6 (not yet downloaded).

## Phases

0. **Baseline.** A machine with a PCIe slot and the card as the boot
   display. Install the current `vbe` package; record the PCI id,
   subsystem, BARs, the VBE mode list and memory size, and run the GDI,
   mode and DirectDraw tests at tier 0. Done means a dated record under
   `docs/decisions/` with the captures. A 64-bit BAR0 placed above 4 GiB
   would be a finding to stop on.
1. **Identity.** `src\chipsets\ati\si\oland_hw16.c` and a row in the
   `ati` manifest for the measured id. A BAR read that takes BAR0 and
   BAR2 beside `V9xPciReadViBars`, and the mini-VDD map for it. The
   family's PCI id table has 41 ati ids; check its capacity in the same
   change.
2. **Read-only register probe.** A `V9XSIMM` in the `V9XCKMM` mould:
   `MC_VM_FB_LOCATION` and the VRAM size, `GB_ADDR_CONFIG` and the fuses
   (CU count; the R7 240 is sold with fewer CUs than the full die),
   `GRBM_STATUS`, `SRBM_STATUS`, the DMA0 and CP ring registers, and the
   first CRTC's surface address against the VBE scanout. Mullins read
   `GB_ADDR_CONFIG` as 0
   ([carry-over](release-0.42.0-carryover.md)); record what Oland's
   reads, and whether the BIOS left anything in reset as Trinity's did
   with `SRBM_SOFT_RESET` bit 16.
3. **DMA write probe.** `si_dma.c`'s ring bring-up on DMA0, no microcode,
   a ring test, then fills and copies checked byte-exact, as Cedar's and
   Trinity's write probes. This is where the byte-versus-dword copy count
   is settled.
4. **DirectDraw on the DMA engine.** By the route chosen above. Done
   means V9XDDP passes at 16 and 32 bpp with the engine claimed and the
   counters show the blits on it.
5. **Flips.** Through `eg_scanout.c`. Done means flips taken by the chip
   in V9XDDP.
6. **CP and 3D (GFX6), its own plan when started.** `gfx_v6_0`'s order:
   golden registers, gpu_init, the RLC and its microcode, PFP/CE/ME
   through their ports, the ring test, the SI clear state, then the
   shaders, with `cik_shader.c`'s encoder checked instruction by
   instruction against the Southern Islands ISA guide. Then V9XD32 at 32
   and 16 bpp, 3DMark 2000, and OpenGL and Glide on the engine.

## Unknowns that need a measurement

- Everything in Phase 0: which id, which BIOS, which modes, whether BAR0
  is 32-bit.
- The engine clocks the BIOS leaves. SI has no SMU path in this plan;
  the card runs at whatever the BIOS set, which may be its lowest state.
  Scores are recorded, not chased.
- Whether the RLC is needed for the CP to run. Trinity's CP ran without
  it; GCN's may not.
- Whether a discrete card's mode changes through the VBE leave the DMA
  engine and the CP in a state the restart path handles, as Carrizo's and
  Mullins' did.

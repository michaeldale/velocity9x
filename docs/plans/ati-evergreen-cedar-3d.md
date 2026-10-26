# Direct3D on the Radeon HD 5450 (Evergreen "Cedar")

Date: 2026-10-26

Status: in progress. Decision 1 taken (Michael, 2026-10-26): the
microcode goes in the tree. Phase 1 done: the CP runs AMD's microcode on
M-BXMFA8, `ME_INITIALIZE` and Linux's ring test pass three runs in three
([record](../decisions/2026-10-26-cedar-cp-runs-amd-microcode.md)).
Next: Phase 2, GPU init and clear state. Parent plan:
[ati-evergreen-cedar.md](ati-evergreen-cedar.md); 2D runs on the DMA
engine ([record](../decisions/2026-10-26-cedar-directdraw-on-dma.md)).
Michael asked for Direct3D next (2026-10-26).

## Why this is a different kind of work from every earlier engine

Every 3D engine in the tree so far takes fixed-function state in
registers or packets: the ViRGE, the Rage 128's CCE, the MGA, Gen3.
Evergreen has no fixed-function pipeline. A triangle needs:

1. **The command processor (CP) running.** It is halted after POST
   (`CP_ME_CNTL` `14000000`,
   [register record](../decisions/2026-10-26-cedar-register-probe.md))
   and runs only AMD's microcode: PFP 1120 dwords and ME 1376 dwords
   (`radeon_ucode.h`), `CEDAR_pfp.bin` and `CEDAR_me.bin` in
   linux-firmware. A discrete Evergreen needs no RLC microcode for this:
   Linux initialises the RLC only on the IGPs (`evergreen_startup`).
2. **The GPU's static configuration**, which the BIOS does not do:
   `GB_ADDR_CONFIG` read 0 on M-BXMFA8. Linux `evergreen_gpu_init` writes
   it and the SQ/SX/SPI/VGT resource splits from Cedar's numbers (1 SE,
   2 SIMDs, 2 pipes, 1 backend, 256 GPRs, 4 contexts).
3. **A clear-state preamble** (`evergreen_default_state`,
   `clearstate_evergreen.h`) and `ME_INITIALIZE`, as `evergreen_cp_start`
   sends them.
4. **Shaders**: a fetch shader, a vertex shader and a pixel shader in the
   Evergreen ISA, uploaded to VRAM. Fixed-function Direct3D state
   (Gouraud, textures and their stages, fog, specular, alpha test) has to
   become pixel-shader variants, as the Haswell plan does with VS kernels
   per state key.
5. **Pipeline state**: colour buffer (`CB_*`), depth buffer (`DB_*`),
   viewport and setup (`PA_*`), vertex grouping (`VGT_*`), shader
   resources (`SQ_*`, `SPI_*`), as `SET_CONTEXT_REG` packets on the CP.

## Sources

Collected so far (`build\external\evergreen\linux-radeon\`, current
`torvalds/linux`): `evergreen.c` (`evergreen_gpu_init`,
`evergreen_cp_load_microcode`, `evergreen_cp_start`,
`evergreen_cp_resume`), `evergreend.h`, `evergreen_cs.c` (the command
checker: which registers and packets userspace may send, with their
layouts).

Wanted, not yet fetched:

- linux-firmware `radeon/CEDAR_pfp.bin` and `radeon/CEDAR_me.bin`, with
  `LICENSE.radeon`.
- Linux `clearstate_evergreen.h` (current), and from an old kernel (v3.9)
  `evergreen_blit_kms.c`, `evergreen_blit_shaders.c/.h`: Linux's own 3D
  blit, a complete minimal pipeline with hand-assembled VS and PS.
- xf86-video-ati `evergreen_accel.c`, `evergreen_exa.c`,
  `evergreen_shader.c/.h`, `evergreen_state.h`, `r600_shader.h`: EXA's
  solid, copy and composite on the 3D engine, with shader assembly
  macros.
- AMD's *Evergreen Family ISA* (rev 1.1a) and *Evergreen 3D Register
  Reference*, both public.

## Phases

0. **Decide the microcode question** (below).
1. **CP bring-up probe**, a write probe like V9XEGDMA: load PFP and ME,
   reset CP/PA/SH/VGT/SPI/SX, a CP ring in the 64 KiB window beside the
   DMA ring, `ME_INITIALIZE`, then a `MEM_WRITE` fence (or a scratch
   register write) and read it back. Proves the CP runs Linux's
   microcode on this card.
2. **GPU init and clear state**: `evergreen_gpu_init`'s writes for Cedar,
   the preamble, and a `SURFACE_SYNC`. Read back with V9XEGMM.
   What the source shows it needs beyond Phase 1's tools:
   - Some registers lie past the 128 KiB window: the shader
     constant-buffer sizes at `28140h`-`28200h` and `28F80h`-`29000h`.
     Linux reaches them through `MM_INDEX`/`MM_DATA` (offsets 0 and 4).
     The probe VxD needs an indirect write op limited to those ranges.
   - The render-backend disable mask comes from the RCU's fuse straps
     (`RREG32_RCU(0x203/0x204)`), an indirect read. Read it before any
     write, and record it.
   - `GB_ADDR_CONFIG` takes Cedar's golden value `02010001`; the same
     value goes to `DMIF_`, `HDP_` and `DMA_TILING_CONFIG`.
     `DMA_TILING_CONFIG` already reads `02011203` from the BIOS and is
     used by the running DMA engine, so the probe must leave it alone or
     measure the difference.
   - Linux 3.9's blit state (`set_default_state`) repeats the SQ
     resource splits as `SET_CONFIG_REG` packets with Cedar's numbers
     (93 PS / 46 VS GPRs, 96 PS / 16 VS threads, 42 stack entries each),
     so the MMIO writes may matter less than the packets. That is the
     first thing to measure: Phase 3's draw with the packet state alone.
3. **A solid rectangle through the 3D engine**: Linux v3.9's blit state
   with a constant-colour pixel shader, into the scratch window, checked
   byte for byte. This is the first triangle.
4. **A textured, Gouraud triangle**, the shader-variant scheme designed
   against Direct3D 5-7's fixed function, host-tested as stream builders
   (like `r128_3d.c` and `hsw_3d.c`).
5. **The HAL**: a `d3d_eg.c` engine behind `V9X_D3D_ENGINE_OPS`, Z
   buffers, then V9XD32 and V9XDDP's 3D checks, then 3DMark 2000.

## Decision taken

1. **Shipping AMD's CP microcode: in the tree** (Michael, 2026-10-26).
   `LICENSE.radeon` permits binary redistribution with its notice and
   disclaimers reproduced, and forbids disassembly.
   `scripts\gen-evergreen-microcode.ps1` generates
   `src\chipsets\ati\evergreen\eg_cp_microcode.c` with the licence text.
   The package's notices must carry it from the first build that links
   the arrays.

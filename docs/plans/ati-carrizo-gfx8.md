# AMD Carrizo (Radeon R7E, GCN 3) in the ati family

Date: 2026-10-28

Status: Phase 0 done (tier-0 `vbe` on M-XP7053,
[record](../decisions/2026-10-28-m-xp7053-carrizo-vbe-first-look.md)).
Phase 1 coded and gated, not installed. Phase 2 read on the chip
([record](../decisions/2026-10-28-carrizo-register-probe.md)): a 512 MiB
carve-out at GPU `F4_00000000` (system `C0000000`), the VBE scanout at its
start, no microcode loaded by anyone, every engine halted, GFX powered
with power gating off. Next: Phase 3. Register facts below come from
Linux `amdgpu` source; the records say which ones the chip has confirmed.

Target: the GPU in AMD's Embedded GX-420GI ("Merlin Falcon", 2015), PCI
`1002:9874` rev 88, which Linux binds as `CHIP_CARRIZO | AMD_IS_APU`
(`v4.9-amdgpu_drv.c:386`). A sixth chip generation in the one `ati`
family, beside Evergreen, in its own module tree `src/chipsets/ati/vi/`
("Volcanic Islands", the name Linux files GFX8 under). Test machine:
`M-XP7053`, an HP board (`103C:8158`), reached only over the relay.

## Sources

Collected 2026-10-28 into `build\external\carrizo\` (outside version
control):

- `linux\`: `torvalds/linux` master at `65ba8f3`, sparse:
  `amdgpu/{vi,gfx_v8_0,sdma_v3_0,gmc_v8_0,amdgpu_ucode,amdgpu_device,cz_ih}.c`,
  `pm/powerplay/smumgr/smu8_smumgr.c`, and the register headers
  `asic_reg/{gca/gfx_8_0,gmc/gmc_8_1,oss/oss_3_0,dce/dce_11_0,bif/bif_5_0,smu/smu_8_0}_*.h`.
- `old\`: single files from older tags, because master no longer has the
  paths this plan needs: `v4.9-gfx_v8_0.c`, `v4.9-sdma_v3_0.c`,
  `v4.9-cz_smc.c`, `v4.9-amdgpu_ucode.c`, `v4.9-vi.c`, `v4.9-amdgpu_drv.c`,
  `v4.9-cz_dpm.c` (direct microcode loading, removed later);
  `v5.15-dce_v11_0.c` (the DCE 11 driver, removed from master); and
  `master-smu8*.h`, `master-smu_ucode_xfer_cz.h`, `master-cz_ppsmc.h`.
- Microcode: `amdgpu/carrizo_{pfp,me,ce,rlc,mec,sdma,sdma1}.bin` are in
  the linux-firmware mirror on gitlab.com (checked 2026-10-28, not
  downloaded). Their licence file was not found under the name tried
  (`LICENSE.amdgpu`); the WHENCE entry has to be read before anything
  ships.

Published by AMD, not yet downloaded: the *GCN3 Instruction Set
Architecture* reference (the shader ISA), on AMD's developer site. No
GFX8 3D register guide or DCE 11 guide was looked for yet.

Register offsets in Linux's `*_d.h` headers are **dword** indices; the
byte offsets below are four times those.

## What the sources settle

| | Carrizo per Linux | Consequence |
|---|---|---|
| Register BAR | BAR5 (config 24h) on every chip from Bonaire on (`amdgpu_device.c:3945`) | A new BAR read. BAR0 is the framebuffer (VBE gave `E0000000`), BAR2 the doorbells, BAR4 I/O. The window must reach `3E05Ch` (CP microcode ports), so it is 256 KiB |
| Video memory | A UMA carve-out of system RAM. `CONFIG_MEMSIZE` 5428h in MiB (`gmc_v8_0.c:522`); the CPU reaches it at `MC_VM_FB_OFFSET` 2068h << 22 on an APU, not through BAR0 (`gmc_v8_0.c:542`) | VBE reports 48 MiB; the carve-out may be larger. BAR0 is still how Windows sees it; whether BAR0 covers all of it is a measurement |
| GPU address of VRAM | `MC_VM_FB_LOCATION` 2024h, 16 MiB units as on Evergreen (`gmc_v8_0.c:379`) | Same decode as Cedar |
| Microcode | **Every** engine needs it: CP (PFP, ME, CE), RLC, MEC, and both SDMA engines (`v4.9-sdma_v3_0.c:54`). Master loads Carrizo's only through the SMU (`amdgpu_ucode.c:584`); 4.9 could also write it straight into the engines' ucode ports with `smc_load_fw=0` (`v4.9-vi.c:1672`, `v4.9-gfx_v8_0.c:4150`, `v4.9-sdma_v3_0.c:747`) | No microcode-free 2D path, unlike Evergreen's DMA engine. The direct path is the first thing to try; the SMU path is the fallback |
| SMU firmware status | `SMU8_Firmware_Header` at SMU address `1001FF80h`, read through `MP0PUB_IND_INDEX` 600h / `_DATA` 604h; `Version` +0, `UcodeLoadStatus` +18h, a bit per engine (`smu8_smumgr.c:146`, `master-smu8.h:31`, `master-smu_ucode_xfer_cz.h:76`) | The read probe can tell whether the system BIOS already had the SMU load anything |
| SDMA | Two engines, 800h apart, at D000h (`oss_3_0_d.h`), ring registers from D200h | 2D copies and fills, once loaded |
| CP | `CP_ME_CNTL` 86D8h, ring 0 at C100h, ucode ports 3E050h-3E064h | 3D, as on Evergreen, but the shader core is GCN |
| Shader ISA | GCN 3: scalar + vector, no VLIW | A new encoder; nothing in `eg_shader.c` carries over |
| Display | DCE 11.0, three controllers at +0, +800h, +1000h (`v5.15-dce_v11_0.c:53`, num_crtc 3). CRTC0's `GRPH_*` and `CRTC_*` sit at the same byte offsets as Evergreen's | `eg_scanout.c`'s flip logic should carry over with new controller offsets |
| GART | Carrizo's GART is 1 GiB (`gmc_v8_0.c:555`); the MC's AGP and system apertures as on Evergreen | Host-to-VRAM copies need it; later |

## Phases

0. **Baseline (done).** Tier-0 `vbe` at boot 17: enable-ok, GDI and
   mode cycles pass, no 8 bpp modes, no EDID.
1. **Identity.** Claim `1002:9874` exactly in the `ati` manifest as a
   tier-0 chip with no hooks (`vi/carrizo_hw16.c`). The INF then names
   it. Other Carrizo ids are not claimed until a survey of one is in hand.
2. **Read-only register probe** (`CZMM.VXD` + `V9XCZMM.EXE`): the BARs,
   `CONFIG_MEMSIZE`, `MC_VM_FB_LOCATION`/`FB_OFFSET`, the apertures,
   GRBM/SRBM status and soft resets, CP, RLC and both SDMAs' state, the
   SMU's firmware version and load status, and all three controllers'
   scanout. Questions: is any microcode already loaded; is the GFX block
   powered (RLC power gating); does the VBE scanout address equal the FB
   start plus the LFB offset; how big is the carve-out.
3. **Microcode, direct.** A write probe loads SDMA0's microcode through
   `SDMA0_UCODE_ADDR/DATA`, unhalts it (`SDMA0_F32_CNTL`), and runs one
   constant fill and one copy from a ring in the carve-out, byte-exact.
   If the ucode port does not take, measure the SMU path instead.
4. **DirectDraw on SDMA**, under a new engine type, packet builders
   host-tested first, as Evergreen's DMA path was.
5. **Flips and vblank** through DCE 11's `GRPH_PRIMARY_SURFACE_ADDRESS`.
6. **CP and 3D**: RLC + PFP/ME/CE microcode, ring test, then a GCN 3
   shader encoder and the clear state, in its own plan.

## Unknowns that need a measurement

- Whether the GFX block, SDMA and RLC are power-gated as the system BIOS
  left them, and what ungating takes on a machine with no SMU driver.
- Whether the direct ucode ports still work on Carrizo silicon, or only
  through the SMU (Linux dropped the direct path; the reason was not
  found).
- The carve-out size and whether BAR0 maps all of it.
- Whether a VBE mode set disturbs any engine state.

## Decisions for Michael

1. Microcode in the tree, as for Cedar: the amdgpu licence has to be read
   first (see Sources).

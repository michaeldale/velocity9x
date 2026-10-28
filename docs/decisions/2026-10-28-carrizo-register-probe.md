# Carrizo register probe: a 512 MiB carve-out, no microcode loaded, GFX powered

Date: 2026-10-28

Status: **measured, one machine, one boot.** M-XP7053's Radeon R7E
(`1002:9874`), boot 17, desktop at 1024x768x16 under the signed 0.32.0
`vbe` driver. Phase 2 of [the plan](../plans/ati-carrizo-gfx8.md). Report
in
[`../probe/m-xp7053-carrizo-register-probe-2026-10-28/`](../probe/m-xp7053-carrizo-register-probe-2026-10-28/).

## The probe

`CZMM.VXD` + `V9XCZMM.EXE` (`scripts/build-carrizo-mmio-probe.ps1`), built
from this tree (`db50efb2-dirty`). It maps BAR5 and reads 197 registers
twice. Its one write is the SMU's indirect index `MP0PUB_IND_INDEX`, read
first and restored. It read `0` before and was put back. Exit 0,
`Result=COMPLETE`, status `22D`. Only `CRTC_STATUS_POSITION` and
`CRTC_STATUS_HV_COUNT` moved between the two reads, as a scanning
controller's counters do.

## What it found

- **BARs.** BAR0 `E000000C` (64-bit prefetchable, the framebuffer, high
  half 0), BAR2 `F000000C` (64-bit, doorbells), BAR4 I/O `F000`, BAR5
  `FEB00000` (32-bit registers), ROM `FEB40000`. The bus address is
  `80000800`, i.e. bus 0, device 1.
- **Video memory.** `CONFIG_MEMSIZE` = 512 MiB, not the 48 MiB VBE
  reports. `MC_VM_FB_LOCATION` puts it at GPU `F4_00000000`-`F4_1FFFFFFF`.
  `MC_VM_FB_OFFSET` = `300h`, so the carve-out sits at system physical
  `C0000000`. The boot record's MTRR 2 covers `C0000000`-`DFFFFFFF`
  write-back, which matches. `HDP_NONSURFACE_BASE` = `F4000000` (the same
  address, >> 8).
- **Scanout.** D1 is enabled at 16 bpp, pitch 1024, viewport 1024x768,
  linear-aligned (array mode 1). Its surface is GPU `F4_00000000`, the FB
  start: VBE scans out from offset 0 of the carve-out, which is BAR0
  offset 0. D2's graphics block is programmed the same way, but its CRTC
  is off. D3 is unprogrammed.
- **Microcode.** The SMU runs firmware 18.1.0 (`Version` `04120100`,
  signature `AA55AA55`). `UcodeLoadStatus` = 0: the system BIOS had it
  load nothing.
- **Engines.** CE, PFP and ME are halted, MEC ME1 and ME2 halted, and the
  RLC is stopped (`RLC_CNTL` 0). Both SDMAs are halted and idle with
  their rings off. `SDMA0/1_VERSION` read `300h`. No engine is held in
  soft reset. `GRBM_STATUS` `00003028`: not GUI-active.
- **Power.** `RLC_GPM_STAT` `16h`: the GFX block is powered and clocked,
  and the RLC is idle. `RLC_PG_CNTL` = 0, so power gating is off.
  `RLC_CGCG_CGLS_CTRL` = `3Fh`: coarse-grain clock gating and light sleep
  are on. Linux turns them off before loading the RLC
  (`v4.9-gfx_v8_0.c:4095`).
- **GFX configuration.** `GB_ADDR_CONFIG` = `22010001`, Linux's
  `CARRIZO_GB_ADDR_CONFIG_GOLDEN`, so the BIOS ran the golden setup.
  `CC_GC_SHADER_ARRAY_CONFIG` reads `FFC00001`; its inactive-CU field was
  not decoded.
- **Memory controller.** GART off (`VM_L2_CNTL` bit 0 clear, context 0
  off). The L1 TLB is enabled (`503h`). The AGP and system apertures are
  all 0, so without a GART the engines can reach only the carve-out.

## What it settles for the plan

- No engine is power-gated, and none needs ungating before a microcode
  load is tried. Clock gating is on, and the direct-load attempt should
  clear it first, as Linux does.
- The microcode load is ours to do: neither the BIOS nor the SMU has done
  it.
- The heap can grow well past the VBE's 48 MiB, up to 512 MiB, if BAR0
  maps the whole carve-out. BAR0's size was not read (that takes a config
  write).
- The DCE 11 scanout uses the same `GRPH_*` layout and the same MC
  address scheme as Cedar's, so `eg_scanout.c`'s logic should carry over
  with a different FB base.

## Not checked

- BAR0's size.
- What a VBE mode switch does to D1/D2 (only one mode captured).
- Whether the SDMA or CP ucode ports accept a load. That is Phase 3's
  write probe.

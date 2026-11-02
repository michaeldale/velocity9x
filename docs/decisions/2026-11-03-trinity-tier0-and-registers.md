# The HD 7660D at tier 0, and its registers: 256 MiB at GPU F_00000000, six SIMDs

Date: 2026-11-03
Status: **Phases 0 and 1 (read) done** on M-D7TFW4's integrated Radeon
HD 7660D (`1002:9901`, Trinity), boot 17, at the owner's 1024x768x16 on
the signed 0.34.0 `vbe` package. Nothing was installed, restarted or
written to the device.
Plan: [ati-trinity-hd7660d.md](../plans/ati-trinity-hd7660d.md).
Captures: [`docs/probe/m-d7tfw4-trinity-vbe-2026-11-03/`](../probe/m-d7tfw4-trinity-vbe-2026-11-03/),
[`docs/probe/m-d7tfw4-trinity-registers-2026-11-03/`](../probe/m-d7tfw4-trinity-registers-2026-11-03/).

## What the owner did

Remote-help chat msg 87 (2026-11-02 15:30Z) asked for the APU's graphics.
By boot 17 the owner had already removed the HD 7470 (or disabled it in the
BIOS) and installed 0.34.0 `vbe` on the APU's devnode (driver date
11-1-2026). Boots 9-16 were not watched; nothing records what happened in
them. The inventory now lists one display device:

| | Boot 3 (2026-10-31, HD 7470 primary) | Boot 17 (now) |
|---|---|---|
| Active display | `1002:6778` Caicos, `vbe` 0.33.0 | **`PCI\VEN_1002&DEV_9901&SUBSYS_070A1025&REV_00`** at 00:01.0, `vbe` 0.34.0 (`Build=d9d7aeb9`) |
| `1002:9901` | "Standard PCI Graphics Adapter", problem 22 | the display |
| RAM to Windows | 467 MiB | 724 MiB |
| MTRRs | uncached 8 MiB at 1E800000h, 16 MiB at 1F000000h | uncached 8 MiB at 2E800000h, 16 MiB at 2F000000h, **256 MiB at 30000000h** |
| Monitor | `AOC2769`, EDID preferred 1920x1080 | the same |

The 256 MiB uncached range probably covers the graphics carve-out at the
top of the first GiB, and its size matches `CONFIG_MEMSIZE` below. That it
is the carve-out is a reading of the MTRRs, not a measurement. Windows
gaining 257 MiB suggests the BIOS's UMA size changed with the swap. That
was not asked.

## Tier 0 on `vbe`

| | Result |
|---|---|
| Stage | `enable-ok`; VBE 3.0, OEM rev `0f0f`, reports 16 MiB |
| LFB | `C0000000` (BAR0), `DpmiMap=01000000:ok` |
| Modes listed / published | 56 / 39, **including 1920x1080 at 8, 16 and 32 bpp** (`Recommendation=1920x1080 reason=edid-preferred`); 1600x1200 and up carry `a=00ba` and are not published, as on the HD 7470 |
| `V9XGDI /auto` at 1024x768x16 | PASS |
| `V9XMSW /set:1920x1080x32`, then `V9XGDI /auto` | PASS, PASS |
| `V9XMSW /cycle:10` | PASS |
| `V9XMSW /depth:10` | PASS |
| `V9XMSW /set:1024x768x16` (the owner's mode back) | PASS |
| `V9XDDP` at 1024x768x16 | `Result=COMPLETE`; `V9XDD.INI` 59 `Ok=1` / 47 `Ok=0`, `V9XDD2.INI` 87 / 33 |
| Desktop screenshot after | clean, 1024x768x16 |

The HD 7470's tier-0 V9XDDP read 60 / 47 and 87 / 33. Every Z-buffer
creation returned `0x88760231` (17), which is the software path's answer.
The one-key difference was not compared rung by rung. The 0.34.0 test
programs ran from `C:\extras\Velocity9x\velocity9x-0.34.0-vbe`.

## The register probe

V9XEGMM (`23f4b53f-dirty`, this branch's change) now accepts `9901` in
both the EXE and EGMM.VXD. For Trinity it skips the discrete memory
sequencer (`MC_SEQ_SUP_CNTL`, `MC_IO_PAD_CNTL_D0`, `MC_SEQ_MISC0`). It adds
the CP's second and third rings, the second DMA engine (DMA0's block plus
800h, `nid.h:1302`) and the RLC load-balancing registers that
`evergreen_rlc_resume` programs on `CHIP_ARUBA` (`evergreen.c:4398`). All
are plain reads. The files are in `C:\V9XDIAG\EGTN` on the guest, so the
HD 7470's copy in `C:\V9XDIAG` is left as it was. 248 offsets were
accepted. Every register read the same twice except D1's
`CRTC_STATUS`, position and H/V counters, and none read `FFFFFFFF`.

| | HD 7660D (now) | HD 7470 (2026-10-31) |
|---|---|---|
| Config address | `80000800` (bus 0, device 1) | `80010000` |
| BAR0 | `C0000008`: 32-bit prefetchable (so 14h, `0000F001`, is an I/O BAR at `F000`, not a high half) | `C000000C`, 64-bit |
| BAR2 (registers) | `FEF00000`, 32-bit | `FEA20004`, 64-bit |
| Probe status | `0000002D`: mapped, identity check passed, snapshot taken | |
| `CONFIG_MEMSIZE` | **256 MiB** | 2048 MiB |
| `MC_VM_FB_LOCATION` | `0F0F0F00`: GPU **`F_00000000`-`F_0FFFFFFF`** | `0F7F0F00` |
| `HDP_NONSURFACE_BASE` | `0F000000` (GPU `F_00000000` >> 8) | |
| D1 scanout | enabled, 1024x768, 16 bpp format 1, pitch 1024, surface `F_00000000`: offset 0 into the carve-out | the same shape |
| D2 | graphics enabled at the same surface, CRTC off | the same |
| `GB_ADDR_CONFIG` | 0 (BIOS does not set it) | 0 |
| `DMA_TILING_CONFIG` / `HDP_ADDR_CONFIG` | `42011203` / `42011003` | `02011203` / `02011003` |
| CP | `CP_ME_CNTL` `14000000`: ME and PFP halted; `CP_RB_BASE`, `CP_RB1_BASE`, `CP_RB2_BASE` hold uninitialised patterns (`FEDCBAEF`, `...DF`, `...CF`) | halted |
| DMA0 / DMA1 | not in reset, rings off, both idle (`DMA_STATUS_REG` `44C83D57`) | DMA0 the same |
| RLC | `RLC_CNTL` 0; every `TN_RLC_*` register 0 | |
| GART | `VM_L2_CNTL` `0605CE02` (bit 0 clear), context 0 off | off |
| Backend disable (`CC_RB_BACKEND_DISABLE` 23:16) | 0: both backends enabled | one backend |
| SIMD disable (`CC_GC_SHADER_PIPE_CONFIG` 31:16) | `FFC0`: **six SIMDs** | `FFFC`: two |

`MC_SHARED_CHMAP` and `MC_ARB_RAMCFG` decode to one 32-bit channel. On an
APU these describe the GPU's side of the system memory path, not DRAM the
card owns. Linux uses them for `vram_width` (bandwidth sums) and for a row
size that `cayman_gpu_init` then replaces with `GB_ADDR_CONFIG`'s. It
hard-codes eight banks on Fusion (`ni.c:1043`). The decode's
`MemoryWidthBits` says nothing about the memory here.

Bit 30 set in `DMA_TILING_CONFIG` and `HDP_ADDR_CONFIG` is the only
difference from the HD 7470's values. Which field it is was not looked up.
Linux overwrites both with the golden `GB_ADDR_CONFIG` (`12010001`,
`nid.h:45`) in `cayman_gpu_init`.

## What it settles, and what it does not

1. **Linux's row for `9901` matches the silicon.** `cayman_gpu_init`
   (`ni.c:902-972`) gives `9901` six SIMDs per SE and two backends, one SE,
   two tile pipes. The fuses read six and two.
2. **VRAM is at GPU `F_00000000`**, as on the HD 7470. The 40-bit work done
   for Caicos (DMA packets, CP streams, `gpu_high`) applies as it stands.
   The heap is the 256 MiB carve-out, not 2 GiB.
3. **D1's graphics block is at Evergreen's offsets.** Linux drives
   Trinity's DCE 6.1 flips with `evergreen_page_flip`
   (`radeon_asic.c:1881`), and the probe's D1 reads match the VBE mode
   exactly.
4. **The registers are in a 32-bit BAR2 below 4 GiB.** The mini-VDD's and
   the HAL's BAR handling for 64-bit BARs is not needed here. Whether it
   copes with a 32-bit one was not checked.
5. Not established: the BAR sizes (sizing takes a configuration write), the
   carve-out's physical address, whether the CP runs without the RLC
   microcode Linux loads on every IGP (`ni.c:2163`, `ARUBA_rlc.bin`), and
   anything about the shader core (VLIW4).

## Owner chat

Msg 87 has no reply. A reply needs Michael's approval first.

# Radeon HD 2600 XT (RV630, 1002:9588) in the ati family

Date: 2026-11-13

Status: Phase 0 done (tier-0 `vbe` on M-NZHWBR,
[audit](../decisions/2026-11-13-w9qi54f0-hd2600xt-audit.md)). Phase 2
read on the card the same day
([record](../decisions/2026-11-13-rv630-register-probe.md)): 256 MiB at
GPU address `E0000000`, VBE scans out through `D1GRPH`, the GPU idle,
the CP's ME halted and the DMA ring disabled. Phase 3's DMA write probe
ran the same day ([record](../decisions/2026-11-13-rv630-dma-write-probe.md)):
copies, inline writes and overlapping copies exact; a fill works only as
a seed write then copies with a CPU-awaited fence between dependent
steps, and a one-dword COPY is a no-op. The rectangle-fill shape for
DirectDraw (seed row inline, a CPU-awaited fence, then every row copied
from it in one submission) measured exact. Claimed in the `ati` family
the same day with DirectDraw fill and copy on DMA (engine type 15,
`eng_r6.c`): V9XDDP's fill, copy and four overlap cases pixel-correct at
16 bpp, the ring drawn through, no timeouts
([record](../decisions/2026-11-13-rv630-directdraw-on-dma.md)). The row
lost the `vbe` driver's software Direct3D (the `ati` family's default
request is 0). 3D through the CP: AMD's RV630 microcode runs
([record](../decisions/2026-11-13-rv630-cp-ring-test.md)), the first
solid draw is exact ([record](../decisions/2026-11-13-rv630-first-3d-draw.md)),
and Gouraud and textured (modulate) draws are exact; depth writes one
constant Z for every fragment, open
([record](../decisions/2026-11-13-rv630-gouraud-texture-depth.md),
[issue](../issues/2026-11-13-rv630-depth-constant-z.md)). Next: the depth
issue's leads, then the Direct3D HAL on `struct v9x_r6_3d_state`. Other register facts
below come from Linux `radeon` source.

Target: the RV630 (`1002:9588`, Gigabyte SUBSYS `1458:216C`) as a new
chip generation in the one `ati` family (`one-family-per-vendor`), with
its own module tree `src/chipsets/ati/r600/`. Test machine: `M-NZHWBR`
(W9QI54F0, Core i5-3570K, Windows 98 SE, DirectX 8.1), reached only
through the remote-help relay. The owner offered the card in chat
message 191.

## Sources

Already in `build\external\` (outside version control):

- `evergreen\linux-radeon\`: `r600.c`, `r600d.h`, `r600_dma.c`,
  `radeon_asic.c` (the `rv6xx_asic` table) and the Evergreen files the
  Cedar work used.
- `rs690\linux-radeon\r500_reg.h`: the AVIVO display block, shared with
  the RS690.
- `rs690\xf86-video-ati-6.14.6\`: the UMS driver, whose R600 EXA path
  (`r6xx_accel.c`, `r600_exa.c`, `r600_state.h`) drives 2D through the
  CP with shaders.
- `audit\mesa\src\gallium\drivers\r600\`: the R600 shader encodings.

To fetch before Phase 3: `RV630_pfp.bin` and `RV630_me.bin` from
linux-firmware (`r600.c` declares them), and AMD's published R6xx
register and 3D programming guides from x.org.

## What the sources settle

| | RV630 per the sources | Consequence |
|---|---|---|
| Family | `CHIP_RV630`, `rv6xx_asic` (`radeon_asic.c:2392`), with RV610/620/635/670 | One tree serves the R6xx parts once this one works; other ids wait for a card |
| Register BAR | BAR2, 64 KiB (Linux maps BAR2 before Bonaire) | The probe maps BAR2, as on the RS690 and Evergreen |
| Memory | `CONFIG_MEMSIZE` 5428h in bytes; `MC_VM_FB_LOCATION` 2180h, start and top >> 24 (`r600_mc_program`) | VBE reports 16 MiB; the real size is the first read |
| 2D engine | **None.** R600 dropped the fixed 2D engine, as Evergreen did (`ati-evergreen-cedar.md`) | No Rage-128-style register path |
| DMA engine | `r600_dma_ring` in `rv6xx_asic`, ring at D000h (`r600_dma.c`); `.copy.dma = r600_copy_dma` | DirectDraw fill and copy on the DMA ring, the Cedar route. Whether its packets match Evergreen's is a Phase 3 check |
| CP | PM4, microcode `RV630_pfp.bin` / `RV630_me.bin`, loaded through `CP_PFP_UCODE_*` and `CP_ME_RAM_*` | 3D needs the CP, as on Cedar; no register-PIO 3D path exists after R500 |
| 3D engine | R600 shader core (Mesa `r600`), the ancestor of Evergreen's; different register offsets and shader encodings | Not shared code with `eg_shader.c`; a port of the R600 forms |
| Display | AVIVO, the RS690's offsets: `D1GRPH_PRIMARY_SURFACE_ADDRESS` 6110h, `D1GRPH_UPDATE` 6144h, `D1CRTC_STATUS` 609Ch (`rs600_page_flip`, `avivo_wait_for_vblank` in `rv6xx_asic`) | Flips and vblank as on the RS690 once the probe shows D1GRPH carrying the VBE mode |
| Mode setting | AtomBIOS | Keep `vbe-lfb` |

## Phases

0. **Baseline (done).** Tier-0 `vbe`: enable-ok, 48 modes, 16 MiB
   reported, no 16:9 mode for the AOC2769.
1. **Identity.** Claim `1002:9588` in the `ati` manifest as a tier-0
   device with no hooks, so the INF names the card.
2. **Read-only register probe** (`V9XRVMM` + `RVMM.VXD`, from the RS690
   one): BARs, `CONFIG_MEMSIZE`, `MC_VM_FB_LOCATION`, `CHMAP`/`RAMCFG`,
   `GRBM_STATUS`, `CP_ME_CNTL` (is the CP halted?), the CP and DMA ring
   registers, the pipe and backend masks, and every D1/D2 AVIVO register.
   Read at the desktop mode, then at three VBE modes. Prove the GPU
   address of the VBE scanout.
3. **DMA write probe**: a fill and an overlapping copy at 8, 16 and 32
   bpp through the DMA ring, byte-exact readback, as Cedar's
   (`2026-10-26-cedar-dma-write-probe.md`). Then DirectDraw on DMA.
4. **Flips and vblank** via D1GRPH, if Phase 2 shows the VBE mode there.
5. **CP and 3D**: microcode load and ring test, then a flat triangle
   with R600 shaders, then the Direct3D HAL on the `r3d` interface, the
   Cedar order.

## Open

- How the R600 DMA packets differ from Evergreen's (`r600_dma.c` against
  `evergreen_dma.c`), read before Phase 3.
- Whether the VBE BIOS can be made to offer a 16:9 mode, or whether the
  AOC panel stays scaled.

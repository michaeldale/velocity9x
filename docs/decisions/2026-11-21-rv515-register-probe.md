# RV515 register probe on M-5CAPM3: 256 MiB, 128-bit, FB at D0000000, VBE on D1GRPH

Date: 2026-11-21
Machine: remote-help `M-5CAPM3` (COOLPC), Radeon X1300 `1002:7146`
subsystem `1002:0322`, on 0.57.0 `vbe` at 640x480x16, boot 35 (the owner
had fitted a 3Com network card in place of the USB adapter).
Probe: `RVMM.VXD` + `V9XRVMM.EXE` from branch `ati-rv515` (c67e39bb's
code, built as `01fe7a27-dirty` before that commit). Reports in
[`docs/probe/m-5capm3-rv515-2026-11-21/`](../probe/m-5capm3-rv515-2026-11-21/).
Plan: [`ati-rv515-x1300.md`](../plans/ati-rv515-x1300.md), phase 2.

## Runs

1. Plain (`V9XRVMM`, nothing written to the device): `Result=COMPLETE`,
   status 6Dh (ATI, mapped, identity, snapshot, 64-bit BAR), 143 offsets.
2. `/mc` (MC indexed reads, `MC_IND_INDEX` restored): `Result=COMPLETE`,
   status 86Dh. Every MMIO register matched run 1 except the CRTC
   position and frame counters (60A0h, 60A4h, 60ACh), which move.
   `MC_IND_INDEX` read 1 before and 1 after, so the restore held.
   `BootCounter` stayed 35 across both runs.

## What the card is

| Fact | Evidence | Reading |
|---|---|---|
| VRAM | `CONFIG_MEMSIZE` (F8h) = `10000000` | 256 MiB. VBE reports 16 MiB, so the VBE figure is a BIOS cap, not the memory |
| Memory bus | `MC_CNTL` (MC 05h) = `00000161`, bits 1:0 = 1 | 128-bit (rv515.c `rv515_vram_get_type`) |
| FB in GPU space | `HDP_FB_LOCATION` (134h) = `0000D000`; `MC_FB_LOCATION` (MC 01h) = `DFFFD000` | VRAM at GPU `D0000000`-`DFFFFFFF`, the same address as BAR0 |
| BAR0 | config 10h = `D000000C`, 14h = 0 | 64-bit prefetchable memory, below 4 GiB; 256 MiB by Device Manager |
| BAR2 (registers) | config 18h = `FDEF0004`, 1Ch = 0 | 64-bit memory, below 4 GiB, 64 KiB aligned |
| BAR4 | config 20h = `0000CE01` | I/O at CE00h |
| Apertures | `CONFIG_APER_0_BASE` `D0000000`, `_1_BASE` `D8000000`, `CONFIG_APER_SIZE` `08000000` | Two 128 MiB apertures inside BAR0. What the second does on this chip (the Radeon line uses it for byte-swapped access) is not checked |
| Expansion ROM | config 30h = 0 | Not enabled in config space at this time |

The RS690 tree's BAR read refuses this card as it stands.
`rs690_hw16.c` reads its BARs with `V9xPciReadEgBars`
(`src\display16\runtime.asm`, comment at 3584), which requires BAR0 256 MiB
aligned and BAR2 128 KiB aligned. BAR0 `D0000000` passes; BAR2
`FDEF0000` is 64 KiB aligned only (`FDEF0000 & 1FFFFh = 10000h`), so the
read fails. An RV515 row needs a BAR read that accepts a 64 KiB-aligned
register window, as this probe's VxD does. The plan's open question is
answered: the Evergreen read does not fit.

## Engines as the BIOS left them

- 2D: `RBBM_STATUS` = `10000140`: GUI idle (bit 31 clear), 64 free FIFO
  entries. `DP_GUI_MASTER_CNTL`, `DEFAULT_OFFSET`/`PITCH`,
  `DST_PITCH_OFFSET` and `SRC_PITCH_OFFSET` all 0: the BIOS never set up
  the 2D engine. A driver has to program the default surface itself.
- CP: `CP_CSQ_CNTL` 0 (queues disabled), `CP_RB_CNTL` 0, `CP_RB_BASE`
  `CDCDCDCC` (uninitialised).
- Pipes: `R400_GB_PIPE_SELECT` = `000001E4`: bits 13:12 = 0, one quad
  pipe by the RS690 decode; bits 11:8 = 1 is the field `rv515_gpu_init`
  folds into PLL 0Dh. `DST_PIPE_CONFIG` (170Ch) = 0, so
  `rv515_gpu_init`'s `pipe_select_current` would be 0. Whether the BIOS
  already set PLL 0Dh as Linux would is not readable here (it is a PLL
  index register; the probe does not touch PLLs).
- `GB_TILE_CONFIG` (4018h) = `00000021`. `VAP_CNTL_STATUS` (2140h) =
  `00020000`: `PVS_BYPASS` (bit 8) clear, as expected on a TCL part; the
  VAP is not in bypass as the BIOS left it.
- MC: `MC_STATUS` (MC 08h) = `0000001F`, idle bit 4 set.

## Display

- D1: CRTC on, GRPH on, `D1VGA_CONTROL` (330h) bit 0 clear (AVIVO, not
  VGA scanout), `GRPH_CONTROL` `00000101` (16 bpp, format 1 = 5:6:5),
  pitch 640 pixels, viewport 640x480, primary surface `D0000000` = FB
  offset 0, no update pending or locked. The VBE mode scans out through
  D1GRPH, so a flip should be a `D1GRPH_PRIMARY_SURFACE_ADDRESS` write,
  as on the RS690. That is inferred; no flip was tried.
- D2: CRTC off.

## What this settles for the plan

- Phase 2's questions are answered except the PLL pipe register and the
  VAP's behaviour, which only a 3D probe can show.
- Phase 6 (heap beyond 16 MiB) has its number: 256 MiB, all in BAR0,
  which the CPU sees whole.
- The frame buffer's GPU address equals its CPU address, so GPU offsets
  for the 2D engine are FB-relative from `D0000000`.

## Phase 0 items closed on the same machine

`V9XDDP` (boot 32, 0.57.0 vbe, software Direct3D): `Result=COMPLETE`,
report split across `V9XDD.INI` / `V9XDD2.INI`. Against the Cedar's
0.30.1 `vbe` run on M-BXMFA8
(`docs/probe/m-bxmfa8-cedar-vbe-2026-10-26/boot3-V9XDD-1024x768x16.INI`),
the `*Ok` keys the two share agree, except that `Tex8A8888HalvesOk` and
`Tex8X8888HalvesOk` pass here and failed there. Failing here and absent
from that older run: `SpriteOk`, `RampOk`, `FlipPixelOk`. FlipPixelOk also
fails on the Oland 6610 and 6611; SpriteOk and RampOk on the software
engine have not been compared with another recent vbe run. The machine
left the relay seconds after V9XDDP started and the run still completed
and wrote both halves, so the loss was the USB network adapter, not a
crash during the run. Boots 33-35 that followed were the owner fitting
the 3Com card.

# The HD 5450's command processor runs AMD's microcode: ME_INITIALIZE and Linux's ring test pass

Date: 2026-10-26

Status: **measured, one card, three runs on one boot.** M-BXMFA8's HD 5450
(`1002:68F9`, Cedar), boot 7, the `ati` driver with DirectDraw on the DMA
engine running ([record](2026-10-26-cedar-directdraw-on-dma.md)). Phase 1
of [ati-evergreen-cedar-3d.md](../plans/ati-evergreen-cedar-3d.md). New
tool: `V9XEGCP.EXE` + `EGCP.VXD` (`tools\diag\evergreen_cp*`,
`scripts\build-evergreen-cp-probe.ps1`). Captures:
[`../probe/m-bxmfa8-cedar-cp-2026-10-26/`](../probe/m-bxmfa8-cedar-cp-2026-10-26/).

## The microcode

linux-firmware's `radeon/CEDAR_pfp.bin` (4480 bytes, 1120 dwords) and
`radeon/CEDAR_me.bin` (5504 bytes, 1376 dwords), fetched from the
project's official GitLab mirror (kernel.org served a bot check).
Their sizes match `radeon_ucode.h`. WHENCE: "Licence: Redistributable.
See LICENSE.radeon for details." That licence permits binary
redistribution with its notice and disclaimers reproduced, and forbids
disassembly. Michael chose to carry the microcode in the tree
(2026-10-26). `scripts\gen-evergreen-microcode.ps1` checks each file's
SHA-256 and generates `src\chipsets\ati\evergreen\eg_cp_microcode.c`
with the licence text, as `r128_cce_microcode.c` carries the Rage 128's.
Nothing links it yet.

## The tool

`EGCP.VXD` is `EGDMA.VXD`'s op-list design with its own allow-lists:

- **Writes:** the registers Linux `evergreen_cp_load_microcode`,
  `evergreen_cp_resume` and `r600_ring_test` write, and
  `GRBM_SOFT_RESET` restricted to the CP, PA, SH, VGT, SPI and SX bits.
- **Stream op:** feeds the request's data area to the two microcode
  data ports only.
- **Scratch window:** BAR0 + 241 MiB, clear of the driver's DMA ring at
  240 MiB.

The EXE reads the CP's registers first and goes ahead only if the ME and
PFP are halted, as POST leaves them. It then runs Linux's sequence:

1. Stop the CP.
2. Load PFP, then ME.
3. Pulse the GRBM reset, then wait 20,000 register reads, where Linux
   waits 15 ms.
4. Start a 16 KiB ring with RB_NO_UPDATE and no writeback.
5. Send `ME_INITIALIZE` (Cedar's 4 contexts), then write `FFh` to
   `CP_ME_CNTL`.
6. Run `r600_ring_test`: `SCRATCH_REG0 = CAFEDEAD` by MMIO, then
   `SET_CONFIG_REG` of `DEADBEEF` through the ring.

At the end it halts the CP, puts every register back and restores the
window. Clear state is not sent.

## Results

| Run | `ME_INITIALIZE` consumed | `SCRATCH_REG0` | `CP_RB_RPTR` | `CP_STAT` | `CP_ME_CNTL` running / after |
|---|---|---|---|---|---|
| 1 | yes, by the first poll | `DEADBEEF` | `20` = write pointer | `80000042` | `00000000` / `14000000` |
| 2 | yes | `DEADBEEF` | `20` | `80000042` | `00000000` / `14000000` |
| 3 | yes | `DEADBEEF` | `20` | `80000042` | `00000000` / `14000000` |

The value in `SCRATCH_REG0` could only have come from the ring packet:
the CPU wrote `CAFEDEAD` there just before. The CP's read pointer is in
dwords (`20h` = 32: two 16-dword submissions). `CP_ME_CNTL` reads 0 while
running though `FFh` was written, so its low byte does not read back.
`CP_STAT` bit 31 stayed set after the work drained; Linux names no bit
there, and this was not followed up.

After the three runs, V9XDDP still passed fill, copy and overlap through
the DMA engine on the same boot. The CP and the DMA engine share neither
registers nor VRAM.

## What this settles

- **The CP runs Linux's microcode on this card.** It loads through the
  data ports from the halted POST state, and it runs PM4 type-3 packets
  from a ring in VRAM.
- **Each session can start from POST's halted state:** the microcode
  reload and the GRBM reset in Linux's resume sequence are enough. No
  extra reset was needed between runs, unlike the DMA engine.

## Not measured

- Clear state (`evergreen_default_state`) and `evergreen_gpu_init`'s
  configuration (`GB_ADDR_CONFIG` is still 0). These are Phase 2.
- Any drawing. No shader has run.
- A CP left running for long, or across a mode set or a DOS box.

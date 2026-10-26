# The HD 5450's DMA engine copies and fills byte-exact; each ring session needs a DMA soft reset

Date: 2026-10-26

Status: **measured, one card, eight runs on one boot.** M-BXMFA8's HD 5450
(`1002:68F9`, Cedar), boot 3, the 0.30.1 `vbe` package at 1024x768x16.
Phase 3 of [ati-evergreen-cedar.md](../plans/ati-evergreen-cedar.md),
first step. New tool: `V9XEGDMA.EXE` + `EGDMA.VXD`
(`tools\diag\evergreen_dma*`, `scripts\build-evergreen-dma-probe.ps1`).
Captures:
[`../probe/m-bxmfa8-cedar-dma-2026-10-26/`](../probe/m-bxmfa8-cedar-dma-2026-10-26/).

## The tool

The VxD executes an op list and refuses anything outside its allow-lists:

- **Reads:** the DMA block, `SRBM_STATUS`/`_STATUS2`/`_SOFT_RESET`,
  `MC_VM_FB_LOCATION`, `CONFIG_MEMSIZE`, `GRBM_STATUS`.
- **Writes:** the registers `r600_dma_resume` writes,
  `HDP_MEM_COHERENCY_FLUSH_CNTL`, and `SRBM_SOFT_RESET` with 0 or the DMA
  bit alone.
- **VRAM:** a 64 KiB scratch window at BAR0 + 240 MiB (GPU `0F000000`),
  saved before the run and restored after. The driver uses the first 16
  MiB.

The EXE reads the block first and goes ahead only if the ring is off, the
block neither busy nor in reset, and VRAM starts below 4 GiB. It lays out
buffers with guard dwords and a 1024-dword ring, starts the ring with no
read-pointer writeback and no interrupts (`DMA_CNTL` bit 28,
`CTXEMPTY_INT_ENABLE`, cleared for the run), and submits two batches, each
padded to 16 dwords with NOPs:

1. a dword copy of 256 dwords; a byte copy of 37 bytes from source byte 1
   to destination byte 3; a copy of 64 dwords to 4 dwords above its source
   (overlapping); a fence;
2. a constant fill of 256 dwords; a fence.

Then it stops the ring, puts every register back, flushes HDP, reads the
buffers back and restores the window.

## Results

| Run | Build | Reset pulse | Fence 1 / 2 (polls) | Dword copy | Byte copy | Overlap | Fill |
|---|---|---|---|---|---|---|---|
| 1 | first | none | 3 / 1 | PASS | PASS | memmove | PASS |
| 2 | second | none | timeout / timeout, `RPTR` 14h | PASS | FAIL | other | FAIL |
| 3 | second | none | timeout / timeout, `RPTR` 0 | FAIL | FAIL | other | FAIL |
| 4-8 | third | before and after | 3 / 1 | PASS | PASS | memmove | PASS |

PASS means every dword of the run and every guard dword read back as
expected. The overlapping copy came out as `memmove` would leave it, not as
a naive ascending copy would.

## The engine after a stopped ring

`DMA_STATUS_REG` read `44C83D57` as the BIOS left it and while each ring
ran it read `44C83556`. Run 1 stopped the ring as `r600_dma_stop` does
(enable bit off) and wrote back `RPTR`/`WPTR` = 0; after that it read
`04C83956`, then `04C83146` after run 2, and stayed there. `SRBM_STATUS2`
bit 5 (`DMA_BUSY`) stayed clear throughout, so Linux's busy test does not
see it.

From that state run 2 fetched one packet (`RPTR` 14h, 5 dwords: the dword
copy, which landed) and stopped at the byte copy; run 3 fetched nothing.

The third build pulses the DMA soft reset (`evergreen_gpu_soft_reset`'s
sequence for the DMA block: ring off, a wait, `SRBM_SOFT_RESET` bit 20 set
and read back, a wait, cleared and read back, a wait) before bring-up and
again at teardown. Each pulse returns `DMA_STATUS_REG` to `44C83D57`, and
five runs in a row pass. Linux stops the memory controller around every
soft reset; this does not, and the display showed nothing wrong in the
screenshots taken before and after.

Hypotheses this kills: that the engine is unusable as POSTed (run 1
passed from POST state); that the constant fill is Cayman only (it passes
on Cedar); that `DMA_STATUS_REG` bit 0 is an idle flag a driver can test
(it stays 0 on a stopped, not-busy engine after one session).

What it does not settle: which part of the stopped-ring state blocks the
next session. Run 1's teardown restored `DMA_CNTL` with
`CTXEMPTY_INT_ENABLE` set while the ring was empty; that or the pointer
writes on a stopped ring are both candidates, and neither was isolated.

## Consequences for the driver

- **Copies and fills work through the DMA engine** with no microcode:
  dword-aligned copies, byte-aligned copies, overlapping copies with memmove
  semantics, and dword fills.
- **The engine starts from a DMA soft reset** each time the driver takes
  it, and is reset again when the driver lets go: at Enable, at Disable,
  and around a DOS box or mode switch until those are measured.
- **Busy is `SRBM_STATUS2` bit 5**, not `DMA_STATUS_REG` bit 0.
- Fences landed after 3 and 1 polls of a VRAM dword: the work above
  (1.4 KiB copied, 1 KiB filled) completes in microseconds. No throughput
  was measured.

## Not measured

- Throughput, and how it compares with the CPU writing the uncached LFB.
- Copies or fills inside the visible desktop. Everything ran in the
  scratch window.
- More than one ring session per reset. Whether a long-lived ring with
  many submissions keeps running was not tried; the driver will need that.
- What a DOS box or a VBE mode set does to a running ring.

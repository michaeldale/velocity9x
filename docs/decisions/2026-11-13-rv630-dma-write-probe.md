# The HD 2600 XT's DMA engine copies byte-exact; a copy does not see earlier writes in its own submission

Date: 2026-11-13

Status: **measured, one card, six runs on one boot.** M-NZHWBR's HD 2600
XT (`1002:9588`, RV630), agent boot 4, the 0.48.0 `vbe` package at
1024x768x16. Phase 3 of [the plan](../plans/ati-rv630-hd2600xt.md), first
step. New tool: `V9XRVDMA.EXE` + `RVDMA.VXD` (`tools\diag\rv630_dma*`,
`scripts\build-rv630-dma-probe.ps1`), built from `79181f7c` with the tool
uncommitted (`-dirty`). Captures:
[`../probe/rv630-dma-probe-2026-11-13/`](../probe/rv630-dma-probe-2026-11-13/).

## The tool

The Cedar probe (`2026-10-26-cedar-dma-write-probe.md`) with R6xx's
differences, all from Linux `r600d.h` and `r600_dma.c`:

- Packet header: command 31:28, count 15:0. One COPY form, dword-sized:
  header, dst, src, then dst bits 39:32 in 23:16 and src's in 7:0. No byte
  copy.
- No CONSTANT_FILL (`r600d.h`: "7xx only"). The fill is built the way a
  driver would have to: an inline WRITE of a seed, then copies doubling
  the run from it.
- The DMA soft-reset bit is `SRBM_SOFT_RESET` bit 12; no `DMA_MODE`; no
  `SRBM_STATUS2`.

The VxD allows reads of the DMA block, `SRBM_STATUS`, `SRBM_SOFT_RESET`,
`MC_VM_FB_LOCATION`, `CONFIG_MEMSIZE` and `GRBM_STATUS`; writes to the
registers `r600_dma_resume` writes, `HDP_MEM_COHERENCY_FLUSH_CNTL`, and
`SRBM_SOFT_RESET` changed in the DMA bit only; and VRAM in a 64 KiB window
at BAR0 + 240 MiB (GPU `EF000000`), saved before and restored after.

Each run: DMA soft reset, ring started as `r600_dma_resume` starts it (no
writeback, no interrupts), submission 1 (a 256-dword copy, an 8-dword
inline WRITE, a 64-dword copy 4 dwords up its own source, a fence), then
the fill, then a fence; ring stopped, reset, every register put back.

## Results

Linux's R6xx tables send buffer moves through the CP, not this engine
(`radeon_asic.c` `rv6xx_asic`: `copy_ring_index` is the GFX ring; from
RV770 on it is the DMA ring). That did not mean the engine is unusable:
**it runs.** Every run, both fences landed within two polls, `RPTR`
reached `WPTR`, and submission 1 was exact.

| Run | Fill seed | Each step its own submission, fence awaited | Copy | Write | Overlap | Fill |
|---|---|---|---|---|---|---|
| 1 | 1 dword | no | PASS | PASS | memmove | 255 of 256 wrong |
| 2 | 1 dword | no (rerun with a dump of the target) | PASS | PASS | memmove | 255 of 256 wrong |
| 3 | 8 dwords | yes | PASS | PASS | memmove | **PASS** |
| 4 | 8 dwords | no | PASS | PASS | memmove | 240 of 256 wrong |
| 5 | 1 dword | yes | PASS | PASS | memmove | 128 of 256 wrong |

PASS means every dword of the run and every guard dword read back as
expected.

What the failed fills left:

- **Run 2** (dump): only dword 128 holds the fill value. Dword 0, which the
  WRITE set, reads as the guard. Dword 128 is where the last copy (128
  dwords from dword 0) begins.
- **Run 4**: the seed (dwords 0-7) and dwords 128-135. The last copy saw
  the seed; none of the copies in between saw what the copy before them
  wrote.
- **Run 5**: every even dword filled, every odd one the guard. The
  1-dword copy (dword 0 to 1) did nothing; every later copy, 2 dwords and
  up, carried the (fill, guard) pair forward exactly.

So:

1. **Inside one submission a copy does not reliably read what an earlier
   packet of that submission wrote** (runs 1, 2, 4). With a fence after
   each step and the CPU waiting on it, it does (run 3). Whether a
   submission boundary without the CPU wait is enough was not tested.
2. **A one-dword COPY is a no-op** on this chip (run 5). Two dwords and up
   copied exactly; odd counts above one were not tested.
3. Run 2's dword 0, which the WRITE set and nothing copied over, read back
   as the guard. Rule 1 does not explain that. A copy that rewrites the
   whole 32-byte line around its destination with stale data would, but
   that is a hypothesis, not tested.

`DMA_STATUS_REG` read `44C83D57` before every run and after every
teardown, `44C83556` while the ring ran: the values Cedar showed. Unlike
Cedar's, bit 0 came back set after each stop.

## What it means for DirectDraw

- Copies (blits) can go on the DMA ring as they are, one COPY per
  scanline: R6xx has no rectangle form. A blit whose source was just
  written by the ring needs a fence the CPU waits for first.
- A solid fill cannot be one packet. The shape run 3 proves: write a seed
  inline, wait for its fence, then copy from it. One scanline of seed
  (at most 2048 dwords at 1600x1200x32, inside a WRITE's 65,535) lets
  every other row of the fill copy from that one row in a single
  submission, since every copy then reads only completed data. That shape
  is untested; it is the next probe.
- Never emit a one-dword COPY; widen it or write it inline.

## Not measured

- A fill with one seed row and many independent row copies in one
  submission (the shape proposed above).
- Whether a submission boundary alone, without a CPU-awaited fence,
  orders a later copy after an earlier write.
- Odd copy counts above one; unaligned addresses.
- Anything at another mode, or a second boot.

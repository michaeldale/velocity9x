# The HD 7660D's DMA engine: byte-exact five of five, with a reset bit kept

Date: 2026-11-03
Status: **Phase 2, the write probe, done** on M-D7TFW4's HD 7660D
(`1002:9901`), boot 17, 0.34.0 `vbe` at 1024x768x16. The driver does not
claim the chip yet.
Plan: [ati-trinity-hd7660d.md](../plans/ati-trinity-hd7660d.md).
Capture: [`docs/probe/m-d7tfw4-trinity-dma-2026-11-03/`](../probe/m-d7tfw4-trinity-dma-2026-11-03/).

## What Linux does differently on Trinity

`trinity_asic` runs its first DMA engine with `cayman_dma_ring`
(`radeon_asic.c:1632`): Evergreen's fence (`evergreen_dma_fence_ring_emit`)
and copies (`evergreen_copy_dma`), at Evergreen's registers. Its bring-up
`cayman_dma_resume` (`ni_dma.c:186`, fetched from Linux v6.6 into
`build\external\evergreen\linux-radeon\`) is `r600_dma_resume` without the
`DMA_MODE` write (`r600_dma.c:164`) and with `CMD_VMID_FORCE` in
`DMA_IB_CNTL`. The probe uses no IBs, so the second does not matter. The
DMA0 soft-reset bit is Evergreen's (`SRBM_SOFT_RESET` bit 20, `nid.h:87`).

## Changes to V9XEGDMA

- **Identity.** EGDMA.VXD accepts `9901`.
- **32-bit BARs.** Both BAR checks accepted only a 64-bit memory BAR with a
  zero high half. They now also accept a 32-bit memory BAR. On a 32-bit
  BAR the next dword is another BAR, so the report's `Bar0High`
  (`0000F001`) is BAR1, an I/O BAR, not a high half.
- **BAR0's size.** The scratch window is BAR0 + 240 MiB, so it is only safe
  if BAR0 covers 256 MiB. Sizing the BAR takes a configuration write.
  Windows' allocation for the devnode does the job instead
  (`HKEY_DYN_DATA\Config Manager\Enum\C29ADC40`, `Allocation`): memory
  `C0000000`-`CFFFFFFF` (BAR0, 256 MiB) and `FEF00000`-`FEF3FFFF` (BAR2,
  256 KiB; the probe maps 128 KiB of it), I/O `F000`-`F0FF`. The window is
  the carve-out's last 16 MiB, GPU `F_0F000000`. VBE uses the first
  16 MiB.
- **No `DMA_MODE` on Trinity.** The EXE skips `DMA_MODE` (it reads 0) in
  the bring-up and in the restore, following `cayman_dma_resume`.
- **`SRBM_SOFT_RESET` bit 16.** The read-only pass found `00010000`: bit 16
  set by the BIOS. Linux's headers do not name it (`nid.h` jumps from
  `SOFT_RESET_SEM`, bit 15, to `SOFT_RESET_VMC`, bit 17). The probe wrote
  the DMA bit alone and then 0, which would have released whatever bit 16
  holds. Linux's `cayman_gpu_soft_reset` read-modify-writes the register
  (`ni.c:1907-1918`) and keeps it. The EXE now pulses the DMA bit on top
  of the value call 1 read. The VxD's rule changed from "0 or the DMA bit
  alone" to "differs from the current value in the DMA bit only", which
  gives the same writes on Cedar and Caicos, whose register reads 0.

## Runs

A `/readonly` pass, then five runs. The probe is `91bd6122-dirty`, in
`C:\V9XDIAG\EGTN`.

| | Every run |
|---|---|
| Before | ring off, `DMA_STATUS_REG` `44C83D57`, `SRBM_STATUS2` 0, `SRBM_SOFT_RESET` `00010000`, `DMA_CNTL` `18421000`, `DMA_IB_CNTL` `80000000`, `DMA_MODE` 0 |
| Scratch window | GPU `F_0F000000` (`ScratchGpuHigh=0F`) |
| Fences | `11111111` and `22222222` landed; read pointers `80h` and `C0h` = write pointers |
| Dword copy, 256 dwords | PASS, 0 mismatches including guards |
| Byte copy, 37 bytes, offsets 1 to 3 | PASS |
| Overlapping copy, 64 dwords 4 up | `memmove` |
| Constant fill, 256 dwords | PASS |
| After teardown | ring off, `DMA_STATUS_REG` `44C83D57`, **`SRBM_SOFT_RESET` `00010000`** both reads |

`DMA_STATUS_REG` read `44C83556` (bit 0 clear) while the ring ran, and
`44C83D57` again after the reset, as on Cedar, where bit 0 is reported but
not tested. The desktop screenshot after the five runs is unchanged.

## Not established

- What bit 16 holds in reset, and whether the driver must keep it. The
  engine works with it set.
- The second DMA engine (DMA1, at +800h). Nothing was written to it.
- Whether the carve-out is cached by the CPU through BAR0. The MTRRs mark
  256 MiB at `30000000h` uncached, which looks like the carve-out's system
  address, but BAR0 is `C0000000`. Both paths reach the same memory. Which
  one coherency depends on was not looked at. The probe's HDP flush
  matches Linux and was enough here.

## Next

The driver claim: `1002:9901` as a row in `cedar_hw16.c` with fill and
copy, after checking that the mini-VDD, the 128 MiB heap map and the
BAR0 + 240 MiB engine window all accept a 32-bit 256 MiB BAR0.

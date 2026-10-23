# Rage 128: the CCE's ring is a bus-master address, not video memory

Date: 2026-10-23

Status: **measured, one card; one hypothesis killed.** A8U4I5's Rage Fury
Pro, boot 535. This is the first step of the bus-master ring Michael asked
for after 0.28.0, for the Rage 128 alone, with no allocator shared with
other chips. Evidence:
[`../probe/a8u4i5-rage128-cce-ring-2026-10-23/`](../probe/a8u4i5-rage128-cce-ring-2026-10-23/).

## The hypothesis

If the CCE could fetch its ring from the card's own memory, the driver
would need no locked system memory. The CPU would write packets into
video memory and move a write pointer.

Two things suggested it:

- Linux's `r128_cce_init_ring_buffer` writes
  `PM4_BUFFER_OFFSET = ring_start | R128_AGP_OFFSET` (bit 25, 32 MiB).
  That reads as if an offset without the bit addresses the 32 MiB of
  local memory.
- Linux always starts the CCE with `NOUPDATE`, so a ring needs no
  read-pointer write-back into system memory.

## The probe

`R128CCE.EXE /ring` (`tools\diag\r128_cce_win32.c`). Its VxD gained one
operation, a bounded register poll (`RC_OP_MMIO_POLL`).

1. A PIO fill runs as the control.
2. The same fill's packets go into a 64 KiB ring at VRAM `0x01100000`,
   in the half the driver does not use, written through the framebuffer.
3. The probe writes `PM4_BUFFER_OFFSET` = `0x01100000` (no AGP bit),
   both pointers 0, and Linux's watermarks (`0x02020204`).
4. It writes `PM4_BUFFER_CNTL` = 192BM | 13 (2^13 qwords) | `NOUPDATE`,
   starts the microengine free-running, and sets `DL_WPTR` = 12.
5. It polls `DL_RPTR` for 12, then waits for PM4 idle.

`BUS_CNTL` was not touched, so bus mastering stayed disabled
(`BUS_MASTER_DIS`, bit 6, set).

| Reading | Value |
|---|---|
| PIO control fill | PASS, 0 mismatched bytes |
| First ring dword, read back | `0000050B`, the first packet header as written |
| `DL_RPTR` poll for 12 | timed out |
| `DL_RPTR` after the poll | `0x626`: ran 1,562 dwords **past** the write pointer |
| `PM4_STAT` | `800100C0`: PM4 busy, GUI active, after the wait limit |
| Target | untouched: 360 bytes, the fill's whole area, still the prefill |

On the timeout the probe reset the engine and stopped the CCE.
`PM4_BUFFER_CNTL` ended at NONPM4 and the GUI idle, and the machine
stayed up.

The CCE fetched something, but not our packets, and it did not stop at
the write pointer.

## ATI's own configuration

The read-only capture from ATI's 4.13.7192 desktop
(`V9XR1MM-ati-desktop-boot520.INI`):

| Register | ATI | The probe |
|---|---|---|
| `PM4_BUFFER_CNTL` | `80000011`: mode 8 (64BM_64VCBM_64INDBM), size 17 (1 MiB), write-back on | `2000000D`: 192BM, 64 KiB, `NOUPDATE` |
| `PM4_BUFFER_OFFSET` | `00000000` | `01100000` |
| `PM4_BUFFER_WM_CNTL` | `40070707` | `02020204` |
| `PM4_BUFFER_DL_RPTR_ADDR` | `0D6B6030` (system memory) | not set |
| `DL_RPTR`, `DL_WPTR` | `0001248E`, `0001248E` (drained) | |
| `BUS_CNTL` | `5033A300`: bus mastering enabled | `7033A340`: disabled |
| `AGP_BASE` | `DA000000` | 0 |
| `AGP_APER_OFFSET` (178h) | `02000000` | |

ATI's ring sits at `PM4_BUFFER_OFFSET` 0, and is 1 MiB. ATI's desktop
starts at `CRTC_OFFSET` `0x10000` in the same capture. If offset 0 meant
video memory, the ring would overlap the desktop. So the offset is a
bus-master address, reached through AGP on this board. That agrees with
the SDK's "VM space" (Software Development Guide, 5.2.3) and with what
the probe saw.

**Killed: the ring in video memory.** `PM4_BUFFER_OFFSET` does not address
local memory. With bus mastering off, the CCE read whatever its bus
access returned and ran past the write pointer.

Not ruled out: the Pro's `PM4_BUFFER_DL_WPTR_DELAY` "PRE" register (RRG
3-218) delays a pointer write. A stale effective `DL_WPTR` would also run
`DL_RPTR` on. That does not rescue the hypothesis: ATI's offset 0 does.

## What a ring needs, then

Locked system memory the card can read by bus mastering, reached either:

- **through the Rage 128's PCI GART.** `PCI_GART_PAGE` (17Ch) holds the
  physical address of a 32 KiB table, one entry a page, mapping up to
  32 MiB (RRG 3-207). This is Linux's `is_pci` path. `PCI_GART_DIS`
  must be 0, and bus mastering enabled in `BUS_CNTL`.
- **or through AGP**, as ATI does: `AGP_BASE` and the chipset's GART.

Either way, the mini-VDD has to allocate locked pages (`_PageAllocate`)
and report their physical addresses. Michael chose (2026-10-23) to build
that for the Rage 128 alone, not as a shared service.

The next measurement is the PCI GART route in the probe's own VxD:

1. Allocate a ring and a table.
2. Point `PCI_GART_PAGE` at the table.
3. Enable bus mastering.
4. Run the same fill.

The CCE only reads (`NOUPDATE`), so a wrong address returns garbage
rather than corrupting memory, as this run did.

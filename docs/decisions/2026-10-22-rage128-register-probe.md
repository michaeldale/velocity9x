# Rage 128 Pro registers, read: ATI runs its command engine as an AGP ring

Date: 2026-10-22

Status: **measured, read-only, one card.** A8U4I5's Rage Fury Pro
(`1002:5046`, 32 MiB AGP), Phase 2 of [ati-rage128.md](../plans/ati-rage128.md).
New tool: `V9XR1MM.EXE` + `R1MM.VXD` (`tools\diag\r128_mmio_probe*`,
`scripts\build-r128-mmio-probe.ps1`). Captures:
[`../probe/a8u4i5-rage128-registers-2026-10-22/`](../probe/a8u4i5-rage128-registers-2026-10-22/).

## The tool

The EXE finds the card by a PCI scan (mechanism #1, CF8h restored) and
the VxD maps BAR2, the 16 KiB register window, and reads an allow-listed
set of offsets twice each. It writes nothing to the card. Before reading
anything else, the VxD checks the read-only copy of configuration space
at 0F00h against the id read from configuration space. It refuses the
indirect data ports (MM_DATA, CLOCK_CNTL_DATA), PALETTE_DATA (its read
advances the index), the CCE FIFO ports at 1000h-13FFh and HOST_DATA.

There are two captures. The first is the non-GUI block, the PM4 status
block and GUI_STAT. The second is the FIFOed 2D and 3D state at
14xxh-1Dxxh. It is taken only when `PM4_BUFFER_CNTL` says NONPM4, or with
`/gui`. Whether a bus read of a FIFOed register waits on an engine the CCE
owns is not documented in any source to hand, and a hang on A8U4I5 needs
Michael at the machine.

## Three captures

| | Boot 519, Velocity9x tier-0 (warm restart from ATI) | Boot 520, ATI 4.13.7192, desktop | Boot 521, tier-0 after a CF9h reset |
|---|---|---|---|
| Identity at 0F00h | `50461002`, matches | matches | matches |
| `CONFIG_MEMSIZE` | `02000000`, 32 MiB | same | same |
| `MEM_CNTL` type | 0, SDR | same | same |
| `CRTC_GEN_CNTL` | `03000400`, 16 bpp, enabled | `03090400` (+ cursor, bit 16) | 16 bpp |
| `CRTC_PITCH` | 100 (800 pixels) | 100 | 100 |
| `CRTC_OFFSET` | 0 | **`00010000`** | 0 |
| `DAC_CNTL` | `FF002002`, 6-bit | `FF001102`, 8-bit | `FF000102`, 8-bit |
| `GUI_STAT` | `00000040`: 64 free, idle | same | same |
| `PM4_BUFFER_CNTL` | 0, NONPM4 | **`80000011`** | 0, NONPM4 |
| `PM4_MICRO_CNTL` | `40003FFF`, free-run | `401F00FF`, free-run | `40003FFF`, free-run |
| `PM4_BUFFER_DL_RPTR` / `WPTR` | 0 / `4A4C` | `1248E` / `1248E` | 0 / `2C502` |
| `PM4_BUFFER_DL_RPTR_ADDR` | 0 | `0D6B6030` | 0 |
| `AGP_BASE`, `AGP_CNTL` | 0, `00700000` | `DA000000`, `00000038` | as boot 519 |
| PCI BARs | BAR0 `DC000008`, BAR1 `D001` (I/O), BAR2 `E1000000` | same | same |

## What it says

**ATI's driver drives the engine through the CCE as a bus-master ring.**
Even at the desktop, with no 3D program running, `PM4_BUFFER_CNTL` is
mode 8, `64BM_64VCBM_64INDBM` (r128_reg.h), with a size field of 11h. The
Linux DRM writes that field as log2 of the ring in quadwords
(`r128_cce.c` start, `size_l2qw`), so 2^17 quadwords, 1 MiB. NOUPDATE
(bit 27) is clear, so the read pointer is written back to physical
`0D6B6030` in system memory. The ring was idle (RPTR = WPTR). The AGP
aperture is at `DA000000`, 32 MiB (`AGP_CNTL` 38h).

**Where ATI's ring is, is not decoded.** `PM4_BUFFER_OFFSET` is 0 without
the `R128_AGP_OFFSET` bit (2000000h) that the Linux DRM sets for an AGP
ring. Read as a video-memory offset, that puts the ring at 0. But the
write pointer is 1248Eh quadwords (about 585 KiB) in, and ATI's desktop
starts at 64 KiB (`CRTC_OFFSET` 10000h). Either the field means something
else under ATI's mode, or the ring sits in memory this reading does not
place. Not established.

**PIO mode is not what ATI uses.** That weakens, but does not kill, the
plan's recommendation of CCE PIO first. PIO still needs no ring memory and
no DMA, so it is still the cheapest thing to try. ATI's choice says the
bus-master ring is the path known to work on this silicon.

**The microengine's free-run bit is set at POST.** After the CF9h reset
(boot 521), under a driver that never writes a Rage 128 register,
`PM4_MICRO_CNTL` reads `40003FFF` with mode NONPM4, as it did after the
warm restart (boot 519). A power-off cold start was not done, so "the
BIOS sets it" is the reading most consistent with this, not a measurement.
With the mode at NONPM4, direct register access is what the engine takes.
That is how aty128fb and X's non-DRI path drive it. So the probe's second
capture runs on the mode alone, and the boot 521 capture read the FIFOed
state with no effect seen.

**The 2D engine has state at POST.** Under tier-0, with no engine code:
`DP_GUI_MASTER_CNTL` `72F034D0` (boot 519) / `74CC34FA` (boot 521),
`DEFAULT_SC_BOTTOM_RIGHT` `02580320` (600, 800: the mode's size),
`DEFAULT_OFFSET`/`DEFAULT_PITCH` 0, `DP_WRITE_MASK` all ones,
`Z_STEN_CNTL_C` 20h, `PLANE_3D_MASK_C` all ones. Phase 3 must program
every engine register it relies on rather than inherit these.

## What Phase 3 takes from this

- Memory size from `CONFIG_MEMSIZE` (00F8h): 32 MiB, as VBE says.
- Identity through BAR2 at 0F00h, for the mini-VDD map.
- The engine is idle and in NONPM4 under tier-0, so direct 2D register
  writes need no CCE teardown on this card after POST. After ATI's driver
  in the same power cycle, `PM4_BUFFER_CNTL` must be put to NONPM4 and the
  microengine stopped first, as `r128_do_cce_stop` does.

## Not established

- Where ATI's ring lives (above).
- Whether ATI's HAL changes mode for 3D. The second capture was not taken
  under ATI, nor with a 3D program running.
- Whether ATI's loaded microcode equals linux-firmware's `r128_cce.bin`.
  Reading it back needs a write to `PM4_MICROCODE_RADDR`.
- The boot 519 captures were taken by the probe's first build, which
  skipped the second capture on the free-run bit; `/gui` forced it.

Gates: `run-checks.ps1` passed on the commit carrying the probe.

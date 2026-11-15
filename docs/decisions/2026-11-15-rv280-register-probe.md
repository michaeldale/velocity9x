# RV280 register probe: the scanout at GPU 0, 128 MiB, the CP off

Date: 2026-11-15

Machine: M-HNBJNV (VICO98): PowerColor Radeon 9250, `1002:5960`,
subsystem `148C:2094`, on a VIA PM133; Windows 98 SE. Driver: the `vbe`
package (`d03a1ab1-dirty`), boot 17. Tool: `V9XR2MM.EXE` + `R2MM.VXD` at
`19ec1531-dirty`, a copy of the RS690's read-only probe
(`tools/diag/rv280_mmio_probe.asm`, `rv280_mmio_probe_win32.c`) with the
RV280's ids and legacy Radeon windows. Nothing is written to the device.
Phase 2 of [`../plans/ati-rv280-radeon-9250.md`](../plans/ati-rv280-radeon-9250.md).

Evidence: [`../probe/rv280-register-probe-2026-11-15/`](../probe/rv280-register-probe-2026-11-15/),
one capture per mode, each register read twice.

## Measured

| | 1024x768x16 | 1024x768x32 | 640x480x8 |
|---|---|---|---|
| `CRTC_GEN_CNTL` depth | 16 | 32 | 8 |
| CRTC width x height | 1024x768 | 1024x768 | 640x480 |
| `CRTC_PITCH` (pixels) | 1024 | 1024 | 640 |
| `CRTC_OFFSET`, `DISPLAY_BASE_ADDR` | 0, 0 | 0, 0 | 0, 0 |
| `RBBM_STATUS` | idle, 64 free | idle | idle |

The same in every mode:
- **`CONFIG_MEMSIZE` reads 08000000h: 128 MiB.** VBE 4F00h reports 16
  MiB. `CONFIG_APER_SIZE` is also 128 MiB, and `CONFIG_APER_0_BASE` is
  `D0000000`, BAR0.
- **`MC_FB_LOCATION` reads `1FFF0000`.** The frame buffer's GPU address
  is 0, so the VBE scanout's GPU address equals its offset in BAR0: 0.
- **CP:** `CP_CSQ_CNTL` mode bits 31:28 read 0, so both queues are off.
- **2D defaults:** `DEFAULT_OFFSET`, `DEFAULT_PITCH`, `DST_PITCH_OFFSET`
  and `SRC_PITCH_OFFSET` are 0. The BIOS sets up no 2D state.
- **Configuration copy:** the dword at 0F00h reads `59601002`, the PCI
  id. The RV280 keeps the configuration copy, which the RS690 does not.
- The pitch equals the width at all three depths, which matches what
  VBE reported (`VbeScanBytes`).

## What it settles

- Direct register writes to the 2D engine are possible as VBE leaves
  the chip. The write probe follows
  ([record](2026-11-15-rv280-2d-write-probe.md)).
- The frame buffer's GPU address is `MC_FB_LOCATION`'s low half << 16,
  the same decode as the RS690's `HDP_FB_LOCATION`. Zero is a real value
  here, which the RS690 code refused.
- A heap beyond VBE's 16 MiB has 112 MiB to grow into (phase 6).

## Not checked

- The 9200SE (`5964`), which was not in the machine.
- `CRTC_OFFSET_CNTL` reads `10000000`. The bit is not decoded here.

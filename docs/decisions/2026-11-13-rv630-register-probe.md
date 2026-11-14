# RV630 register probe: 256 MiB at E0000000, VBE on D1GRPH, CP halted, DMA off

Date: 2026-11-13

Status: Phase 2 of [the plan](../plans/ati-rv630-hd2600xt.md), first
read. Machine M-NZHWBR (Radeon HD 2600 XT, `1002:9588`), 0.48.0 `vbe`
driver at 1024x768x16. Probe `V9XRVMM.EXE` + `RVMM.VXD` built from
`f0def052` with the probe sources uncommitted (`f0def052-dirty`),
read-only: no register written. One capture, at the desktop mode as
found: [`../probe/rv630-register-probe-2026-11-13/`](../probe/rv630-register-probe-2026-11-13/).

## Results

The VxD accepted all 134 offsets (`Status=6D`: mapped, identity matched,
snapshot taken, BAR2 64-bit). Every register read the same twice except
the D1 position counters.

### PCI

| | Read | Meaning |
|---|---|---|
| Location | `80010000` | bus 1, device 0, function 0 |
| BAR0 | `E000000C` | 64-bit prefetchable memory, the frame buffer (VBE's LFB) |
| BAR2 | `F7E20004` | 64-bit memory, the register BAR; high dword 0 |
| BAR4 | `0000E001` | I/O at E000h |
| ROM | `F7E00000` | disabled |
| Command | `0007` | I/O, memory, bus master on |

### Memory

- `CONFIG_MEMSIZE` = `10000000`: **256 MiB**, against the 16 MiB the VBE
  BIOS reports and the `vbe` driver uses.
- `MC_VM_FB_LOCATION` = `00EF00E0`: the frame buffer sits at GPU address
  **`E0000000`** to `EFFFFFFF`, the same address as BAR0 in the CPU's
  space. `HDP_NONSURFACE_BASE` = `00E00000` agrees (address >> 8).
- `CHMAP` NOOFCHAN field 1 and `RAMCFG` CHANSIZE set: two 64-bit
  channels by `r600_mc_init`'s table, a 128-bit bus.
- The system aperture and AGP registers read 0; the VM L2 is
  configured (`VM_L2_CNTL` `0000E002`), no context enabled.

### Display

D1 scans out through AVIVO graphics, not the VGA path: `D1VGA_CONTROL`
0, `D1CRTC_CONTROL` and `D1GRPH_ENABLE` set, `D1GRPH_CONTROL` depth 16,
format 1 (RGB565), pitch 1024 pixels, viewport 1024x768, surface
`E0000000` (offset 0 into the frame buffer). So, as on the RS690, **a
flip is a write of `D1GRPH_PRIMARY_SURFACE_ADDRESS`** with `E0000000`
plus the page's LFB offset. `D1GRPH_UPDATE` showed no pending update and
no lock.

`D1CRTC_H_TOTAL` = 897h and `D1CRTC_V_TOTAL` = 464h: 2200 x 1125 totals,
CEA 1920x1080 at 60 Hz. The BIOS appears to drive the AOC panel at its
native mode and scale the VBE mode to it, as on the RS690. Inferred from
the totals; the scaler was not read.

D2's graphics block mirrors D1's, with its CRTC disabled (totals 320h x
20Dh, a 640x480 timing left programmed).

### Engines

| Register | Read | Meaning |
|---|---|---|
| `GRBM_STATUS` | `00003030` | GUI idle |
| `CP_ME_CNTL` | `10000000` | **ME halted**, PFP not |
| `CP_RB_BASE` | `FEDCBAEF` | never set; `CP_RB_CNTL` 0 |
| `DMA_RB_CNTL` | `00000000` | **DMA ring disabled** |
| `DMA_STATUS_REG` | `44C83D57` | bit 0 DMA_IDLE set; other bits not decoded |
| `CC_GC_SHADER_PIPE_CONFIG` | `00F8FC00` | quad pipes 0-1 active, SIMDs 0-2 active |
| `CC_RB_BACKEND_DISABLE` | `00000001` | backend-disable field (23:16) 0; bit 0 not decoded |
| `GB_TILING_CONFIG` | `00008A12` | not decoded |

The BIOS left the CP and DMA engine as Linux finds them before its own
bring-up: nothing running that the driver would have to stop first.

## What it settles for the plan

- Phase 3's DMA ring starts from a disabled, idle engine. Its ring and
  any target can live in the 240 MiB above VBE's 16 MiB.
- Phase 4 (flips) needs no further read: D1GRPH carries the VBE mode at
  `E0000000`.
- `read_video_memory` can report 256 MiB from `CONFIG_MEMSIZE`, as the
  RS690 and Evergreen rows do.

## Not measured

- Other VBE modes (the RS690 probe read three; D1 already answers the
  question they were for).
- The scaler (`D1SCL_*`) and which connector D1 drives.
- BAR0's size; Windows' LogConfig was not read.
- Any write.

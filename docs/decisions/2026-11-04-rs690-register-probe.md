# RS690 register probe: VBE scans out through D1GRPH, the 2D engine is idle, the CP is off

Date: 2026-11-04

Status: Phase 2 of [the plan](../plans/ati-rs690-x1200.md), first read.
Machine M-GB8KRE (ATI RS690 IGP, `1002:791F`), signed 0.34.0 `vbe`
driver. Probe `V9XRSMM.EXE` + `RSMM.VXD` from branch `ati-rs690` at
`953b2963`, read-only: no register written, `MC_INDEX` included.
Captures: [`../probe/rs690-register-probe-2026-11-04/`](../probe/rs690-register-probe-2026-11-04/),
one per VBE mode (the desktop as found, then `V9XMSW /set` to
1024x768x16, 1280x1024x32 and 640x480x8). Desktop left at 1024x768x16.

## Results

The VxD accepted all 145 offsets at every mode (`Status=6D`: mapped,
identity matched, snapshot taken, BAR2 64-bit). Every register read the
same twice except the CRTC position counters.

### PCI

| | Read | Meaning |
|---|---|---|
| Location | `80012800` | bus 1, device 5, function 0 |
| BAR0 | `F000000C` | 64-bit prefetchable memory, the frame buffer (VBE's LFB) |
| BAR2 | `F8100004` | 64-bit memory, the 64 KiB register BAR; high dword 0 |
| BAR4 | `00009001` | I/O at 9000h |
| BAR5 | `F8000000` | memory, 1 MiB in Windows' LogConfig. **Not identified**: no source to hand names an RS690 BAR5 |
| Subsystem | `053A1002` | |

The dword at 0F00h reads 0: the RS690 keeps no copy of configuration
space there (the Rage 128 did). The VxD's identity check rested on
`CONFIG_MEMSIZE` alone, as designed.

### Memory

- `CONFIG_MEMSIZE` = `08000000`: **128 MiB**, against the 16 MiB the VBE
  BIOS reports and the `vbe` driver uses.
- `CONFIG_APER_0_BASE` = `F0000000` (BAR0), `CONFIG_APER_SIZE` =
  `04000000` (64 MiB). Windows' LogConfig gives BAR0 128 MiB; which of
  the two the CPU can actually reach was not tested.
- `HDP_FB_LOCATION` = `7800`: the frame buffer sits at GPU address
  **`78000000`**, 1,920 MiB, which is 2 GiB less 128 MiB. Consistent with
  a UMA carve-out at the top of the 2 GiB of RAM below 2 GiB; the MC's own
  `MCCFG_FB_LOCATION` was not read (it needs an `MC_INDEX` write).
- `MC_FB_LOCATION` / `MC_AGP_LOCATION` (0148h, 014Ch, the older Radeon
  names) read 0: not this chip's registers, as rs690.c implies.

### Display

At every mode D1 scans out through AVIVO graphics, not the VGA path:
`D1VGA_CONTROL` bit 0 clear, `D1CRTC_CONTROL` and `D1GRPH_ENABLE` set,
and `D1GRPH_CONTROL`, `D1GRPH_PITCH` and the viewport follow the mode:

| Mode | Depth / format | Pitch (pixels) | Viewport | Surface |
|---|---|---|---|---|
| 1024x768x16 | 16, format 1 (RGB565) | 1024 | 1024x768 | `78000000` |
| 1280x1024x32 | 32, format 0 (ARGB8888) | 1280 | 1280x1024 | `78000000` |
| 640x480x8 | 8, format 0 (indexed) | 640 | 640x480 | `78000000` |

The surface address is the frame buffer's GPU base every time, so the
VBE LFB at BAR0 offset 0 is what D1 shows. **Flips are a write of
`D1GRPH_PRIMARY_SURFACE_ADDRESS`** with `78000000` plus the page's LFB
offset. `D1GRPH_UPDATE` showed no pending update and no lock.

`D1CRTC_H_TOTAL` = 897h and `D1CRTC_V_TOTAL` = 464h at every mode:
2200 x 1125 totals, the CEA timing of 1920x1080 at 60 Hz. The EDID asks
for 1920x1080, so the BIOS appears to drive the monitor at its native
mode and scale each VBE mode to it. That is inferred from the totals;
the scaler registers (`D1SCL_*`) were not read.

D2's graphics block mirrors D1's settings with its CRTC disabled.

### Engines

| Register | Read | Meaning |
|---|---|---|
| `RBBM_STATUS` | `10000140` | GUI idle, 64 command FIFO entries free |
| `RBBM_SOFT_RESET` | `00000000` | no block held in reset |
| `CP_CSQ_CNTL` | `00000000` | CP queues disabled; `CP_RB_BASE` holds `CDCDCDCC`, never set |
| `DP_GUI_MASTER_CNTL`, `DST_/SRC_PITCH_OFFSET`, `DEFAULT_*` | 0 | 2D state never programmed |
| `DP_CNTL` / `DP_DATATYPE` | `3` / `00010F06` | |
| `DST_PIPE_CONFIG` | `80000000` | |
| `R400_GB_PIPE_SELECT` | `00000100` | **one** pipe (bits 13:12 = 0) |
| `VAP_CNTL_STATUS` | `00080000` | `PVS_BYPASS` clear; Linux and UMS set it before use |
| `GB_ENABLE`, `GB_TILE_CONFIG`, `GB_SELECT`, `GB_AA_CONFIG`, `RB3D_DSTCACHE_CTLSTAT` | `BD207F32`, `46EB1D5E`, `482FBA7F`, `68CBF332`, `0EB872AD` | Arbitrary-looking values, identical in all eight reads |

The last row fits registers that hold whatever they powered up with
because nothing has written them since; it is a hypothesis. It would also
fit a block that is clock-gated and returns stale data. The Phase 5 init
sequence writes every one of them, so either way this matters only if a
write does not stick.

## What this settles for the plan

- The 2D engine is idle and unclaimed under VBE: Phase 3's write probe can
  start without a reset.
- Flips (Phase 4) need no mode-setting work: VBE already scans out
  through `D1GRPH`.
- The heap can grow past 16 MiB (Phase 6): 128 MiB carved out, of which
  at least 64 MiB is behind BAR0 by `CONFIG_APER_SIZE`.
- The GPU address of every surface is its LFB offset plus `78000000`.

## Not measured

- `MCCFG_FB_LOCATION`, `K8_FB_LOCATION` and sideport presence (MC
  indirect registers).
- Whether the CPU can reach past 64 MiB of BAR0.
- What BAR5 is.
- The scaler registers, and what the monitor shows.

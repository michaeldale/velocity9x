# The Voodoo4's stock driver tiles its desktop at the top of memory

Date: 2026-10-11. Machine: A8U4I5, boot 407. Card: retail 3dfx Voodoo4 4500
AGP, `121A:0009` subsystem `0004121A` revision 01, AGP bus 1. Driver: 3dfx
1.04.00. Evidence:
[a8u4i5-voodoo4-registers-2026-10-11](../probe/a8u4i5-voodoo4-registers-2026-10-11/README.md).
Tool: `VSA100.EXE` + `VSA100.VXD`. Plan:
[3dfx-vsa100-family.md](../plans/3dfx-vsa100-family.md), Phase 1 step 1
(partial: 640x480 only). Register meanings:
[vsa100-registers.md](../specifications/vsa100-registers.md).

## Measured

Identity and resources:

- One chip: functions 1-7 of the device read `FFFFFFFF`.
- Config Manager ranges: memory `C8000000` and `D8000000`, 128 MiB each;
  `D0000000`, 64 KiB; C0000h, 32 KiB; I/O `D000`, 256 bytes; plus the VGA
  legacy ranges. Live BAR0 = `C8000000`.
- Option ROM: "Voodoo4 4500 BIOS - Version 1.15", Elpin, 08/17/00, 32 KiB.

Memory: `dramInit0` = `581EA9A9`, `dramInit1` = `40240031`. 4 parts of
64 Mbit, 32 MiB, SDRAM.

Clocks:

- Misc output `CF`: pixel clock select 3, the PLL.
- `pllCtrl0` = `D137` decodes to 25.176 MHz; CRTC 800x525 total, 640x480
  visible, so 59.94 Hz.
- `pllCtrl1` = `E721` decodes to 166.806 MHz, the 166 MHz "Graphics Clock
  Speed" 3dfx Tools shows.
- `pllCtrl2` = `8090CB00` has M = 0 and is not decoded.

Desktop, 16 and 32 bpp at 640x480:

| | 640x480x16 | 640x480x32 |
|---|---|---|
| `vidProcCfg` | `09040081` | `090C0081` |
| desktop format code (bits 20:18) | 1 | 3 |
| desktop tiled (bit 24) | 1 | 1 |
| hardware cursor (bit 27) | 1 | 1 |
| `vidDesktopStartAddr` | `01F60000` | `01EC0000` |
| `vidDesktopOverlayStride` bits 13:0 | 10 | 20 |
| tile control / compare | `000A3F60` / 0 | `00145EC0` / 0 |
| first tiled page | `1F60` | `1EC0` |

Other registers, unchanged between the two modes: `pciInit0` `01841B20`,
`miscInit0` `40000000`, the `miscInit1` address `4007FFF0`, `agpInit0`
`8000049E`, `tmuGbeInit` `00000FF0`, `vgaInit0` `00001140`, `vgaInit1`
`1FE00000`, `strapInfo` `00FF754F`, `dacMode` 0, status `0000005F` (bit 9
clear). Two reads of every register agreed.

The selector protocol worked as on the Mini-Me: a plain `lfbMemoryConfig`
read returned tile control, the control and compare selectors returned
different values, and the probe put the control selector back.

## What this shows

1. **3dfx's driver puts the desktop at the top of memory, tiled, and moves
   the linear/tiled boundary to its first byte.** At 16 bpp the desktop
   starts at `01F60000` and the first tiled page is `1F60`; at 32 bpp both
   move to `01EC0000`. The tiled region is the desktop rounded up to
   64 KiB: 640 KiB at 16 bpp (614,400 bytes used), 1.25 MiB at 32 bpp
   (1,228,800 used). Everything below stays linear.
2. **In tiled mode the stride field counts 128-byte units.** 10 at 1280
   bytes a line, 20 at 2560. The Mini-Me's linear desktop held bytes
   (`500` = 1280). The unit is inferred from two points; the fact sheet
   has to confirm it.
3. **The PLL decode holds on a second board and a second register.**
   `pllCtrl1` decodes to the clock 3dfx Tools reports, and `pllCtrl0` to
   59.94 Hz with the CRTC totals.
4. **The memory decode holds:** 32 MiB, as the INF and the board say.

## Hypotheses this kills or changes

- **"The BIOS's linear aperture ends at 32 MiB"** (plan, from the Mini-Me)
  describes the BIOS mode only. Under 3dfx's driver the boundary follows the
  desktop. Tier-0 sets modes through VBE, so the BIOS-mode value is still
  the one that matters to Phase 2, and this card's BIOS-mode value has not
  been read.
- **`miscInit0` = 0 is not what a running driver leaves.** 3dfx's driver
  sets bit 30, the bit that makes the `miscInit1` address alias
  `vip2vmiCtrl` (VelocityNT's reading of the spec). So `4007FFF0` may not be
  `miscInit1`. VelocityNT refuses to run when `miscInit0` has bit 30 set;
  Velocity9x must not copy that refusal without asking what 3dfx's driver
  means by setting it.
- **`vgaInit0` bit 12 is set** under the stock driver. The research notes
  say it stops VGA refresh, worth about 10% on blits. Not confirmed here.

## Also observed

- **Only 640x480 is offered.** `1024x768x16` and `1024x768x32` were refused
  with `DISP_CHANGE_BADMODE`. `HKLM\Enum\MONITOR` has a "Plug and Play
  Monitor" entry for every earlier card in this machine and none for the
  Voodoo4, so Windows has no monitor for this adapter. That is the likely
  limit; untested.
- **The vertical lines Michael sees on the monitor are not in video
  memory.** The agent's screenshot at 640x480x16 is clean, and a row across
  the desktop background is one colour. The lines are added in scan-out,
  the cable, or the monitor's sampling. Not yet narrowed down.

## Not established

- The BIOS-mode tile boundary and registers on this card (Phase 2's VBE
  path).
- Any mode above 640x480, and the VBE mode list.
- The 2D and command-FIFO state (`/engine` not run).
- What `miscInit0` bit 30 and the value at `14h` mean under this driver.
- The cause of the lines and of the Quake 3 crash.

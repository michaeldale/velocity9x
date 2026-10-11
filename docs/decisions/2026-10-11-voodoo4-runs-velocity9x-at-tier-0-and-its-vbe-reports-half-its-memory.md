# The Voodoo4 runs Velocity9x at tier-0, and its VBE reports half its memory

Date: 2026-10-11. Machine: A8U4I5, boot 409. Card: retail Voodoo4 4500 AGP,
`121A:0009` subsystem `0004121A` revision 01, BIOS 1.15. Package: `3dfx`
family, chip `vsa100`, branch `vsa100-tier0` at `7710579` (build id
`ddb095a-dirty`). Evidence:
[a8u4i5-voodoo4-velocity9x-2026-10-11](../probe/a8u4i5-voodoo4-velocity9x-2026-10-11/).
Plan: [3dfx-vsa100-family.md](../plans/3dfx-vsa100-family.md), Phase 2.

## Install

Not Have Disk. The seven driver files were put into `C:\WINDOWS\SYSTEM`, a
new class key `Display\0014` was written with the values the generated INF
writes (`TOV9X.REG`), and the devnode's `Driver` value was moved from 3dfx's
`Display\0013` to it. `TO3DFX.REG` moves it back; 3dfx's key, the devnode
and the display settings were exported first (`DISP13.REG`, `ENUMV4.REG`,
`CFGDISP.REG`).

## Measured

- **First boot: `Stage=enable-ok`**, 640x480x16, pitch 1280, aperture
  `D8000000` (BAR1) from VBE 4F01h.
- `V9XGDI /auto`: PASS.
- `V9XMSW /set:` 1024x768x16, 800x600x16, 640x480x8, 640x480x16: each PASS,
  `ChangeResult=0`. Under 3dfx's driver 1024x768 had been refused.
- The agent screenshot is the desktop, drawn correctly.

The BIOS mode, read by `VSA100.EXE` under Velocity9x:

| | Velocity9x (VBE 111h) | 3dfx 1.04.00, 640x480x16 |
|---|---|---|
| misc output | `E3`, fixed 25.175 MHz clock | `CF`, PLL at 25.176 MHz |
| `vidProcCfg` | `30040C81` | `09040081` |
| desktop | linear at 0, stride 1280 bytes | tiled at `01F60000`, stride 10 |
| tile control / compare | `3FFF` / 0 | `000A3F60` / 0 |
| first tiled page | `1FFF` | `1F60` |
| `miscInit0` | 0 | `40000000` |

VBE: version 3.00. The BIOS lists 33 modes; 27 were cached and 18 published
(the seven baseline rows and eleven of the BIOS's 8 and 16 bpp modes,
320x200 to 1280x1024). 4F00h reports **16 MiB**.

## What this shows

1. **VBE 4F00h reports 16 MiB on a 32 MiB card.** `dramInit0` decodes 32 MiB
   on this board, measured twice. The DirectDraw heap is therefore built on
   half the memory. This is the case the `read_video_memory` hook in the
   plan exists for, as the MGA-2164W's measured walk was for its BIOS's 4 of
   8 MiB.
2. **The BIOS leaves the last 4 KiB of the linear view tiled.** Tile control
   `3FFF` puts the first tiled page at `1FFF`: linear BAR1 ends at
   `01FFF000`, one page short of 32 MiB. The Mini-Me's BIOS did the same.
   Usable linear memory in a VBE mode is 33,550,336 bytes, not 32 MiB. While
   4F00h says 16 MiB nothing reaches that page; the hook has to clamp to it.
3. **The BIOS mode differs from 3dfx's driver's in every scan-out register**
   compared, including `vidProcCfg` bits 28 and 29, set here and clear under
   3dfx's driver.

## Not established

- Whether the monitor's vertical lines are present under Velocity9x;
  nobody has looked at the monitor on this boot.
- Any mode switch beyond the four above; refresh rates other than the BIOS
  default.
- The D3 event of boot 407, recorded separately in
  [docs/issues/2026-10-11-voodoo4-found-in-d3-under-3dfx-driver.md](../issues/2026-10-11-voodoo4-found-in-d3-under-3dfx-driver.md).

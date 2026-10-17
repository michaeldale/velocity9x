# Haswell tier-0: enable-ok on the i5-4570, and the hangs that were slow boots

2026-10-17. i5-4570, HD Graphics 4600 (`8086:0412` rev 06), Dell board,
Windows 98 SE over a USB wifi key. Evidence:
`docs\probe\hsw-4570-config-2026-10-17\`.

## Result

The `vbe` family, from `build\win98se-vbe` of the 0.19.0 tree (build
`0ffed944-dirty`), installed through Have Disk on the IGD and reached
`Stage=enable-ok` at 640x480x8 (`V9XBOOT.INI`). Measured there:

- VBE 3.00, `TotalMemory` 511 x 64 KB; the driver uses 16 MB of it.
- 12 modes listed, 12 queried, 12 cached: 640x480, 800x600 and
  1024x768 at 8, 16 and 32 bpp, plus 017D-017F duplicates of 1024x768.
  Every one has its linear framebuffer at `E0000000`, which is GMADR as
  `V9XHSW.INI` decoded it. 800x600x8 has a pitch of 832, not 800.
- No EDID through VBE (`Edid=none`); the monitor is on the VGA port
  through a VGA-to-HDMI converter.
- The desktop draws; an agent screenshot reads it back.

## What looked like a hang, and was not

Three boots after the install showed no signal on the capture card and no
network for four to six minutes, and were power-cycled. The fourth was left
alone and came up by itself. The boot logs say the boot was only slow:

| Boot | Log span (ticks) | Mini-VDD init | Wifi start | TCP/IP start + enumerate |
|---|---|---|---|---|
| stock VGA | 846 | - | 65 | 201 + 326 |
| VGA fallback (FAIL_TOO_LATE) | 946 | - | 65 | 236 + 356 |
| Velocity9x, enable-ok | 877 | 112 | 64 | 255 + 330 |

Every boot of this machine spends 500 to 600 ticks in TCP/IP on the wifi
adapter, with or without Velocity9x. The tick unit was not calibrated
against a clock. The mini-VDD's `Dynamic init` (VBE collection through the
BIOS at `Device_Init`) costs 112 ticks, which is the one cost this driver
adds; it is real and worth shortening, but it is not the slowness.

The interrupted boot's log (`boot-interrupted-BOOTLOG.TXT`) ends at
`Starting Ethernet Connection I217-LM (NDIS2)`, which looked like an NDIS2
interaction. `net start` was commented out of AUTOEXEC.BAT for the next
boot, which also took more than four minutes and also completed. The
ethernet start is therefore **not shown** to hang; the log simply ended
where the power was cut. `net start` stays commented out because this
machine's network is the wifi key and a real-mode NDIS2 driver is what
breaks the netbook's restarts.

The boot after an interrupted one showed the other failure, recorded in the
Enum key (`enum-igd-too-late.REG`): `FailReason=0x0B`, "Cant start theVGA,
after init complete" - FAIL_TOO_LATE in the Windows 98 DDK's multimon
table. Windows did not start the IGD in the boot pass, VDD refused it after
init, `V9XDISP.DRV` was never loaded and the desktop came up on `vga.drv`.
This followed every interrupted boot and is Windows' recovery from it, not
something the driver did; ASD records for the IGD were all `00`.

Nothing on screen before the desktop is visible through the capture path:
the startup menu, ScanDisk and the boot logo are 70 Hz modes the converter
does not show. Not established whether that is the converter or this GPU's
text-mode timing.

## Machine changes made

- `C:\MSDOS.SYS` `[Options]`: `BootMenu=1`, `BootMenuDefault=2` (Logged),
  `BootMenuDelay=3`, `AutoScan=0`, by `V9XMSDOS.EXE`; original in
  `C:\MSDOS.V9X`. After a failed boot Windows still defaults its menu to
  Safe Mode.
- `C:\AUTOEXEC.BAT`: `net start` commented out; original in
  `C:\AUTOEXEC.NET`.
- Agent 0.9.1 to 0.10.0 (`dev-vncfix2`), for `reset`.

## What this disputes

- "The vbe family hangs Haswell at boot" - three observations said so, and
  all three were boots cut short.
- "The NDIS2 ethernet hangs with the mini-VDD" - the only evidence was a log
  ending at that line, and the one boot without NDIS2 was just as slow.

## Not checked

- Modes other than 640x480x8, and any mode switch.
- DirectDraw: `V9XDDH.INI` says `Stage=setinfo-callback-missing`, which has
  not been looked at.
- How long a boot takes in seconds.

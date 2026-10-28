# Tier-0 `vbe` on an AMD Radeon R7E (Carrizo, 1002:9874): enable-ok, GDI passes

Date: 2026-10-28

Status: **tier-0 passed** on the first boot with the driver, restarted by
`reset`. Whether the monitor shows what GDI reads back is not known.
Captures: [`../probe/m-xp7053-carrizo-vbe-2026-10-28/`](../probe/m-xp7053-carrizo-vbe-2026-10-28/).

## The machine

- Remote-help machine M-XP7053 (computer `W9QI1BD3`), registered
  2026-10-28, driven only over the relay.
- An HP board (subsystem `103C:8158`), AMD Embedded GX-420GI, 2 GiB, SanDisk
  32 GB SSD on R. Loew's AHCI driver (ports 1-5 code 10). AMI BIOS dated
  05/13/15, build stamp 02/29/24. RTL8168 on the NDIS2 wrapper.
- Display: PCI `1002:9874` rev 88, the GPU in the APU (GCN 3). Before the
  install it ran MSDISP's Standard PCI Graphics Adapter (VGA) at 640x480x4.
- Agent 0.12.0, updated to 0.13.1 over the relay (hot, no restart) before
  the install.

## Install

- The signed 0.32.0 release `velocity9x-0.32.0-vbe.zip`, unmodified,
  SHA-256 checked against `releases/0.32.0/SHA256SUMS.txt`. Pushed to
  `C:\V9XPKG`; five files fetched back and hash-matched.
- Device Manager, Update Driver, "Display a list", Have Disk, `C:\V9XPKG`.
  The one model offered was "Velocity9x VBE-generic display (any VESA VBE
  2.0+ adapter)". Restart prompt declined; `WININIT.INI` held the two Glide
  renames and no `NUL=` line. `reset`: boot counter 16 to 17, desktop ready.
- The agent's full-colour screenshot of the 4 bpp VGA desktop was noise on
  both 0.12.0 and 0.13.1; `-Gray` captures were readable and drove the
  wizard. Not investigated.

## Boot 17

- `Stage=enable-ok` at 640x480x16, pitch 1280, LFB `E0000000` (BAR0),
  `DpmiMap=01000000:ok`. `V9XHW.INI`: generic VESA adapter, PCI `1002:9874`,
  acceleration none, Direct3D software.
- VBE 3.0 (OEM revision `0f30`), 48 MiB reported (`mem=768`), 16 MiB used.
  18 modes listed, all 16 and 32 bpp, up to 1600x1200; those above
  1024x768 have attribute bit 0 clear (not supported as configured). No 8 bpp
  mode, so the baseline 8 bpp rows are hidden (`scan-contradicted`) and the
  default mode moved to 16 bpp. 800x600 has a padded pitch (1664 at 16 bpp).
- No EDID (`Edid=none`). MTRRs: write-back below 3.75 GiB with the LFB
  range uncached by default.
- `V9XDDH.INI` `Stage=setinfo-callback-missing`, as on every tier-0 capture.

## Tests run through the agent

| Test | Result |
|---|---|
| `V9XGDI /auto` at 640x480x16 | exit 0 (report overwritten by the later runs) |
| `V9XMSW /set:1024x768x16`, then `V9XGDI /auto` | exit 0, exit 0 |
| `V9XMSW /set:1024x768x32`, then `V9XGDI /auto` | exit 0, `Result=PASS` (kept) |
| `V9XMSW /cycle:10` | PASS, 10 of 10 |
| `V9XMSW /depth:10` | FAIL at its first step: 8 bpp, `ChangeResult=-2` |

The depth test asks for 8 bpp, which this BIOS does not offer, so the
failure is the mode list, not the driver. Desktop left at 1024x768x16.

## Not measured

- What the monitor shows. No owner report yet.
- DirectDraw, Direct3D and OpenGL probes (`V9XDDP`, V9XD32).
- Anything beyond `vbe`: GCN 3 has no 2D engine either, and its command
  processor needs AMD's microcode.

# What VelocityNT measured on the Mini-Me VSA-100

Date: 2026-10-11. Machine: DESKPRO (Compaq Deskpro, Pentium III ~1 GHz,
Windows 2000 SP4), not a Velocity9x machine. Board: Mini-Me v2, one
VSA-100, PCI. Source: VelocityNT, the author's closed NT/2000/XP driver,
records dated 2026-10-10 and 2026-10-11, repository revision `26fe6f0`.
Plan: [3dfx-vsa100-family.md](../plans/3dfx-vsa100-family.md), Phase 0
item 1.

This record imports measurements, so that Velocity9x code and tests can
cite evidence held in this tree. It imports no code. The values are
physical measurements under VelocityNT's own miniport, or under the 3dfx
vendor driver where marked. Their raw logs stay in VelocityNT; the names of
the VelocityNT records are given so the evidence can be found there. For
Velocity9x they describe a sibling board: chip-level semantics are expected
to carry to the retail Voodoo4 4500, board-level values are not.

## Identity and apertures (`mini-me-live-identity`, `m5a-deskpro-inventory`, `m5b-void-cleanup`)

- PCI `121A:0009`, subsystem `121A:0002`, revision `01`. Agent inventory
  names the video BIOS "Voodoo4 4500 LCD" 1.18; the ROM was not dumped.
- `0002121A` is the Voodoo5 5500 AGP's subsystem in 3dfx's INF, on a
  one-chip board: subsystem does not give chip count.
- BAR0 `40000000`, 128 MiB, non-prefetchable. BAR1 `50000008`, 128 MiB,
  prefetchable. I/O `1000-10FF`. IRQ 16.
- Vendor driver 5.00.2195.0232: 1024x768x32 at 60 Hz, reports 64 MiB
  (WMI). A stale `0005121A` (Voodoo4 4500 PCI) registry entry is present.

## Memory (`mini-me-offscreen-pages`)

- `dramInit0` `607EADF9`, `dramInit1` `40240031`: 4 parts of 128 Mbit,
  64 MiB, SDRAM.
- Every DWORD of [1 MiB, 64 MiB) saved, tested with two address-distinct
  patterns and complements, restored and re-checked. Pages 32 MiB apart did
  not alias.

## Linear aperture (`mini-me-upper-memory-aperture`, `mini-me-upper-memory-sparse`)

- After the BIOS mode set: tile control `3FFF`, compare `0`, first tiled
  page `1FFF`, so linear BAR1 ends at `01FFF000`.
- `lfbMemoryConfig` read protocol: write `40000000` then read for control,
  `60000000` then read for compare. With bit 30 set a write changes no
  data; the Napalm spec's prose says otherwise and its bit table agrees with
  the hardware.
- Writing compare `8000C000` with desktop fetch paused (only `vidProcCfg`
  bit 7 cleared) moved the boundary to 64 MiB. Compare and `vidProcCfg`
  restored; five mode/reset pairs and the console return passed after.

## First mode (`mini-me-post-mode-capture`, `mini-me-first-mode-timing`, `mini-me-vga-output`)

- VBE `4F02h` BX `4111h` returned `004Fh` by register-only INT10. Windows
  2000's `VideoPortInt10` cannot transfer buffers, so `4F00h`/`4F01h` were
  never read (`sp4-bios-transport`).
- `vidProcCfg` `30040C81`, `vidScreenSize` `001E0280`, `vidDesktopStartAddr`
  `0`, `vidDesktopOverlayStride` `500` (1280 bytes, linear), `miscInit0`
  `0`, `miscInit1` `27008001`.
- Fixed 25.175 MHz VGA clock (misc output `E3`), `pllCtrl0` `00000C01`
  unused. CRTC `5F/4F/0B/3E/40/DF/E3`, `CR1A`/`CR1B` `00`, `dacMode` 0:
  800x525, 59.940 Hz. The user confirmed the picture on a BenQ T2200HD over
  VGA.
- Eleven text resets and console round trips passed pixel regression.

## Status and idle (`m5c-aperture-transition`)

Status bit 9 is the aggregate busy bit. Idle: three consecutive clear
reads, 10 µs apart, at most 256 polls; all-ones means the device is gone.
Twelve reads covered one aperture transaction, no timeouts.

## Not measured there

VBE controller and mode information, the ROM, EDID, any mode except
640x480x16, any refresh rate or PLL in use, 2D/3D/FIFO, Windows 9x, AGP.

## Use in Velocity9x

`tests\host\test_vsa100_decode.c` uses the memory, tile and PLL vectors
above. The retail Voodoo4 4500's first capture
([2026-10-11-voodoo4-stock-driver-tiles-its-desktop-at-the-top-of-memory.md](2026-10-11-voodoo4-stock-driver-tiles-its-desktop-at-the-top-of-memory.md))
confirms the memory decode, the selector protocol and the PLL formula on a
second board.

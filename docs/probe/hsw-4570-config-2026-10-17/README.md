# i5-4570 (Haswell GT2): identity, config space, first vbe install

2026-10-17. Dell desktop, i5-4570, HD Graphics 4600. Windows 98 SE
(computer W9QI5A83), v9x agent at 10.0.1.254:9869 over a Netgear WG111v3
USB wifi key (the same key and DHCP reservation the netbook used).

## Identity (`HSWPCI.REG`, Enum\PCI)

| Device | Id | Note |
|---|---|---|
| 00:00.0 host bridge | `8086:0C00` rev 06 | Haswell desktop |
| 00:02.0 IGD | `8086:0412` rev 06, SUBSYS `05A41028` | HD 4600, "Standard PCI Graphics Adapter (VGA)" |
| 00:03.0 | `8086:0C0C` | HD audio controller (no driver) |
| 00:14.0 | `8086:8C31` | xHCI, no driver |
| 00:1A.0, 00:1D.0 | `8086:8C2D`, `8C26` | EHCI, Windows 98 drives both |
| 00:19.0 | `8086:153A` | I217-LM, real-mode NDIS2 E1000 via `net start` |
| 00:1F.2 | `8086:2822` | SATA in **RAID** mode, not IDE; Windows 98 runs from the BIOS disk path |

Lynx Point (8-series) PCH. EHCI is present, as expected for 8-series.

## Config space (`V9XHSW.INI`)

Read by `V9XHSW.EXE` (tools\diag\hsw_survey_win32.c), mechanism #1 from a
Win32 process, build 0ffed944-dirty. 64 dwords per device, each read twice;
`UnstableReads=0000`.

The Phase 1 decode accepts it (`Status=0000 Refused=0000`):

| Fact | Value | Check |
|---|---|---|
| GGC (00:00.0 50h) | `0209` | mirror at 00:02.0 50h equal |
| GMS | 01h = 32 MB data-stolen | |
| GGMS | 2 = 2 MB GTT-stolen, 512 Ki entries, reaches 2 GB | |
| GGCLCK | 1 | locked |
| IVD | 0 | IGD claims VGA |
| BAR0 GTTMMADR | `F7800004`, high 0 | MMIO `F7800000`, GTT `F7A00000` |
| BAR2 GMADR | `E000000C`, high 0 | aperture `E0000000` |
| MSAC | `02` | 256 MB |
| BDSM (00:02.0 5Ch) | `DD200001` | equals host B0h |
| BGSM (00:00.0 B4h) | `DD000001` | = BDSM - 2 MB |
| TOLUD (00:00.0 BCh) | `DF200001` | = BDSM + 32 MB |

Three registers the BIOS wrote independently (BGSM, BDSM, TOLUD) agree with
the GGC sizes, which is the cross-check the decode was written to make.

## vbe install

`build\win98se-vbe` (run-checks of the same tree) pushed to `C:\V9XPKG`.
Device Manager, Update Driver, Have Disk:

- With **Show compatible hardware**, Have Disk on `C:\V9XPKG` answered "The
  specified location does not contain information about your hardware".
- With **Show all hardware**, the same Have Disk listed "Velocity9x
  VBE-generic display (any VESA VBE 2.0+ adapter)" under *Show compatible
  devices* (`have-disk-match.png`), and it installed.

Why the first list refused it is not established. The model's only ID is the
compatible `PCI\CC_0300`.

Restart declined; the pending `WININIT.INI` held only SETUPX's own
`glide2x`/`glide3x` renames (no `NUL=`). Agent hot-updated 0.9.1 to 0.10.0
(dev-vncfix2) to get `reset`, then `reset` was issued (17:23). The machine
had not come back on the network six minutes later.

## What followed

Three boots after the install were power-cycled as hangs; the fourth, left
alone, reached `Stage=enable-ok` (`V9XBOOT.INI`, `V9XMODES.INI`,
`V9XHW.INI`, `desktop-enable-ok.png`). Comparing boot logs shows every boot
on this machine is slow, Velocity9x or not. The reading, and the machine
changes made along the way (MSDOS.SYS, AUTOEXEC.BAT), are in
`docs\decisions\2026-10-17-haswell-tier0-enable-ok.md`.

| File | Boot |
|---|---|
| `boot-stock-vga-BOOTLOG.TXT` | Standard PCI Graphics Adapter (VGA) |
| `boot-too-late-BOOTLOG.TXT` | Velocity9x bound, refused FAIL_TOO_LATE, `vga.drv` fallback |
| `boot-interrupted-BOOTLOG.TXT` | Velocity9x loaded, power cut at the NDIS2 ethernet start |
| `boot-enable-ok-BOOTLOG.TXT` | Velocity9x, `net start` disabled, enable-ok |

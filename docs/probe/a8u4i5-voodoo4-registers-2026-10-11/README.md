# Voodoo4 4500 AGP register captures, A8U4I5, 2026-10-11

Card: retail 3dfx Voodoo4 4500 AGP, PCI `121A:0009` subsystem `0004121A`
revision 01, AGP bus 1 device 0. Driver: 3dfx 1.04.00 (INF date
11-21-2000), class key `Display\0013`. Boot 407. Record:
[2026-10-11-voodoo4-stock-driver-tiles-its-desktop-at-the-top-of-memory.md](../../decisions/2026-10-11-voodoo4-stock-driver-tiles-its-desktop-at-the-top-of-memory.md).

| File | What |
|---|---|
| `VSA100-640x480x16.TXT` | Probe at the desktop as found. |
| `VSA100-640x480x32.TXT` | After `V9XMSW /set:640x480x32` (result PASS). |
| `VSA100-640x480x32-after-refused-*.TXT` | After `V9XMSW /set:1024x768x16` and `x32`, both refused with `ChangeResult=-2`; the desktop stayed 640x480x32. Repeat captures. |
| `V9XMSW-*.INI` | The mode switcher's result for each request. |
| `VSA100-ROM-IMAGE.BIN` | The 32 KiB option ROM image from the C0000h shadow (length byte 40h), SHA-256 `C20548653748993FB144F085072D449503EBEE08EB3710B2D8EA4FD541453E9C`. "Voodoo4 4500 BIOS - Version 1.15", Elpin, 08/17/00. |
| `ENUMPCI.REG` | `HKLM\Enum\PCI` export. |
| `screenshot-boot407-640x480x16.png` | Agent screenshot (framebuffer read through GDI) while the monitor showed vertical lines. |

Tool: `VSA100.EXE` + `VSA100.VXD` built by `scripts\build-vsa100-probe.ps1`
from the working tree on `6ef71f3` (build id `6ef71f3-dirty`). Default flags:
init and video registers only, `lfbMemoryConfig` selector reads on, no 2D or
FIFO window.

Reproduce: `put` both files to `C:\V9XDIAG`, then
`v9xctl exec -Application C:\V9XDIAG\VSA100.EXE -WorkingDirectory C:\V9XDIAG`
and `get C:\V9XDIAG\VSA100.TXT` and `VSA100.ROM`.

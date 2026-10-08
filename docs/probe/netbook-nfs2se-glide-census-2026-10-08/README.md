# Netbook: Need for Speed II SE's Glide calls, 2026-10-08

- Machine: MICHAEL-NETBOOK (HP Mini 110, Atom N280, GMA 950), Win98 SE,
  Velocity9x 0.13.0 set, boots 120-126. Agent at 10.0.1.248 (ethernet)
  from boot 125; 10.0.1.254 (wifi) before.
- Record: [NFS II SE's Glide census](../../decisions/2026-10-08-nfs2se-glide-census.md).
- Plan: [glide-2x-wrapper.md](../../plans/glide-2x-wrapper.md), Phase 0.

| File | Boot | What it is |
|---|---|---|
| `B120-MEMSTAT.TXT` | 120 | `GlobalMemoryStatus` with 2 GiB installed: `dwAvailPageFile=0` |
| `B120-NFS2SEA-INITMEMMAN-ABORT.png` | 120 | `NFS2SEA.EXE` refusing to start: its heap was never made |
| `B121-MEMSTAT.TXT` | 121 | The same after `MinPagingFileSize=262144`: unchanged |
| `B122-MEMSTAT.TXT` | 122 | The same after `MaxPhysPage=40000` (1 GiB): 981 MiB of page file free |
| `memstat.c` | - | The probe: `GlobalMemoryStatus` and `GetDiskFreeSpace` to `C:\V9XDIAG\MEMSTAT.TXT`, GUI subsystem |
| `B125-INSTALL.WIN` | 125 | The retail installer's `install.win`, Maximum install from the DAEMON Tools drive D: |
| `B125-NFS2SEN-RACE.png` | 125 | The software build in a race (colours are the 8-bit capture's, not the game's) |
| `B126-V9XGLIDE.LOG` | 126 | The census: `NFS2SEA.EXE` on the census `glide2x.dll` (build 63ef2cb-dirty), menus, then a single race on the default settings, about 110 s |

The census DLL draws nothing; the screen stays black throughout. Its
swap waits 16 ms an interval, so the game ran at about 40 swaps a second.
Frames after swaps 1, 2, 1800 and 3600 are logged call by call (the
last two cut at 3,000 lines); outside them each distinct argument set
is logged once, and every export's count every 15 s.

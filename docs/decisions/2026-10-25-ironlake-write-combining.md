# Write-combining the Ironlake's aperture: FbWrite 32 to 3,992 MB/s, 3DMark 2000 1246 to 2909

Date: 2026-10-25

Status: implemented and measured on W9QI2F4F (remote-help machine
M-J0F0SJ: BULLDOZER ZX-H55M, i3-540, Ironlake HD Graphics 8086:0042,
Win98 SE, desktop 1024x768x32), boots 69-73.

Evidence: [`../probe/ironlake-write-combining-2026-10-25/`](../probe/ironlake-write-combining-2026-10-25/).
The Gen3 change it extends: [2026-10-25-intel-gen3-write-combining.md](2026-10-25-intel-gen3-write-combining.md).

## What the BIOS leaves (`V9XBOOT.INI`, boot 69)

```
Mtrr=cpu=000f cap=00001d08 def=00000c00 n=8 r=0 s=5 b=d0000000 z=01000000
Mtrr0=00000006 80000800    WB, 0 - 2 GiB
Mtrr1=80000006 c0000800    WB, 1 GiB at 2 GiB
Mtrr2=c0000006 f0000800    WB, 256 MiB at 3 GiB
Mtrr3=c4000000 fc000800    UC, 64 MiB at C4000000
Mtrr4=c8000000 f8000800    UC, 128 MiB at C8000000 (stolen memory)
Mtrr5..7 = 0               free
```

The GMADR at D0000000 lies under the UC default, and no pair touches it.
Stage A already planned pair 5 over 16 MiB.

## What changed

`v9x_ilk_hd_device.write_combine` = 1. The window is the family's
16 MiB mapping (the GMA 950's rule), inside the 256 MiB GMADR. The drain
was already in place: Ironlake submits through the Gen3/Gen4 ring helper
(`v9x_d3d_i9xx_ring_submit_mode`), whose TAIL write drains since
5ca96ad4, and its batches, vertices and state sit in the reserve, inside
the mapping.

## Boots

| Boot | Build | `MtrrWrite=` |
|---|---|---|
| 69 | the boot-56 set (cd04a7b8-era), uncached | - |
| 71 | this branch at a1ab8164 + this change | `ok s=5 b=d0000001 m=ff000800` |
| 72 | same files, `C:\V9XDIAG\NOWC.TXT` | `off-marker` |
| 73 | NOWC renamed to NOWC.OLD | `ok s=5 b=d0000001 m=ff000800` |

Deployed by WININIT rename over the relay, restarts by `reset`. The
agent went from 0.12.0 to 0.13.1 by hot `update` at boot 69. The
boot-56 files are in this session's scratchpad, `ilk-before-wc`.

## Measured (dispbench 0af3316, 1024x768x32)

| | Boot 69 (old build) | Boot 72 NOWC | Boot 71 WC |
|---|---:|---:|---:|
| FbWrite MB/s | 32.2 | 32.2 | **3,992.1** |
| FbRead MB/s | 28.4 | 28.4 | 107.6 |
| Gdi.FillRect.1024x768 cyc/px | 379.47 | 379.38 | 3.07 |
| Gdi.BitBltDibToScreen.1024x768 cyc/px | 376.61 | 375.48 | 3.84 |
| Gdi.TextOut.FixedOpaque cyc/string | 1,830,239 | 1,831,140 | 37,138 |
| Gdi.BitBltScreenToScreen.256x256 cyc/px | 786.26 | 786.65 | 244.94 |
| Dd.BltFast.SysToVid.Plain.512x512 cyc/px | 190.15 | 190.12 | 4.15 |
| Dd.BltFast.VidToVid.Plain.512x512 cyc/px (engine) | 4.27 | 4.27 | 4.27 |
| DB3D TexUpload.Lock cyc/byte | 188.18 | 187.99 | 2.89 |
| DB3D Clear.Target cyc/px (engine) | 1.59 | 1.59 | 1.59 |
| DBGDI wall time | timed out at 653 s | 112 s | 51 s |

| | Boot 72 NOWC | Boot 71 WC |
|---|---|---|
| V9XD32 /ddi6, /ddi5 | PASS, PASS | PASS, PASS, every value equal to NOWC's |
| 3DMark 2000 default | 1246 | 2909 |

Readings:

- **The Ironlake's uncached aperture writes are the slowest measured in
  this project**: 32 MB/s, a third of the netbook's 105. Write-combined
  they reach 3,992 MB/s, 124 times faster, and every CPU-drawn operation
  follows: GDI fills and DIB drawing about 100 times, texture upload 65
  times.
- **Reads triple (28 to 108 MB/s)**, where the netbook's did not move.
  WC reads are uncached, so this is not a cache. A guess, unmeasured:
  this platform's uncached reads may be serialised behind uncached
  writes in a way WC lifts.
- **3DMark 2000 more than doubles.** On this chip the Direct3D path
  writes every batch and vertex through the aperture, so its uncached
  32 MB/s was what the game tests ran at. The 1244 recorded for boot 55
  on the old build matches the uncached figure.
- **The control boot reproduces the old build to within 0.3%**, so the
  gain is write-combining, not anything else in the newer driver.
- **Engine blits and clears do not move.** dispbench flags them as
  claimed but not delivered against the new ceiling, the same artefact
  as on the netbook.

## Open

- dispbench's first upload after a fresh boot timed out over the relay
  three times (boots 71 and 72); a retry worked each time. The relay,
  not the driver: it happened on the uncached boot as well.
- One board, one boot each way. Frames not watched (no capture on this
  machine).
- WCLOSE.EXE was missing in C:\V9XDIAG and has been copied there from the
  netbook.

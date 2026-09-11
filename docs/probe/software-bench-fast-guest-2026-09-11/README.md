# The software rasterizer on the fast guest

`V9XSOFT` from the same build on two 86Box guests on 2026-09-11, which is what
[the new guest's section in vm-environment](../../vm-environment.md) reports.

| File | The machine it came from |
|---|---|
| `trio64-pentium200-trio64.ini` | `Win98SE-Trio64`, port 9871: Pentium MMX 200, 430TX, S3 Trio64 PCI 4 MiB |
| `fast-d3d-celeron533-virgedx.ini` | `Win98SE-Fast-D3D`, port 9878, boot 584: Celeron Mendocino 533, 440BX (ASUS CUBX), S3 ViRGE/DX PCI 4 MiB |
| `fast-d3d-v9xhw.ini` | that boot's `V9XHW.INI`: `Direct3DMode=software`, `ColourLayout=565-auto` |
| `fast-d3d-celeron533-voodoo3-vbe.ini` | the same guest and CPU at boot 587 with `gfxcard = voodoo3_3500_agp` on the `vbe` package |
| `fast-d3d-voodoo3-v9xhw.ini` | that boot's `V9XHW.INI`: `ModeSwitching=vbe-lfb`, `VbeVramBytes=16777216`, `PciDeviceId=0005` |

The ViRGE/DX and Voodoo3 files are the pair that isolates the aperture: same
machine, same CPU, same binary, and the RAM column compares **1.00 on every
rung**, so every difference in the VRAM column is the bus and the card.
Reads come back 2.09x quicker on AGP and writes 0.61x, which is why the
textured rungs gain and the untextured fills lose.

```powershell
./scripts/compare-software-bench.ps1 `
  -Baseline docs\probe\software-bench-fast-guest-2026-09-11\trio64-pentium200-trio64.ini `
  -Candidate docs\probe\software-bench-fast-guest-2026-09-11\fast-d3d-celeron533-virgedx.ini
```

All 24 pixel and depth hashes match between the two files. That is the check
that the two runs are the same rasterizer on different machines, and it is
worth making before reading any timing out of a cross-machine pair.

The reading itself: about 3.6x to 5.0x on a RAM target, 1.4x to 1.8x on a
video-memory one, and 0.95x - a fraction slower - for an aperture read. The
instrument links the rasterizer directly and never loads the HAL, so this
measures the renderer and the aperture, not the driver.

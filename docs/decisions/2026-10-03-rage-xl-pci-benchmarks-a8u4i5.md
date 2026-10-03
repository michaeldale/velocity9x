# Rage XL PCI on Velocity9x in A8U4I5: Half-Life 18.1, Quake 2 11.6, 3DMark 99 1225

Date: 2026-10-03. Machine: A8U4I5 (Pentium III 1002 MHz, 440BX, Win98SE),
boot 181, the card swapped in that evening: ATI 3D Rage XL PCI
`1002:4752`, subsys `80081002`, rev 27, 8 MB. Velocity9x HAL, DISP and
ICD from tree `e3bf6d3-dirty` (the source of `eca481d`); Mach64 engine
path; desktop 1024x768x16. Evidence:
`docs/probe/a8u4i5-rage-xl-pci-2026-10-03/` (`hl-d3d-mwd5/`,
`q2-gl-timedemo/`, `3dmark99-640/`).

Recorded at Michael's request with a comparison against the other cards.
Per the 2026-09-26 rule these are recorded, not chased.

## Procedures

The same as the earlier runs, so the columns are like for like:
- Half-Life 1.1.1.0 `timedemo mwd5`, `-d3d -w 640 -h 480 -full`, three
  runs.
- Quake 2 3.14 demo, `+set vid_ref gl +set timedemo 1` with the saved
  config used on the Rage IIC.
- 3DMark 99 Max at 640x480 16-bit, 16-bit Z, triple buffer, Pentium III
  optimisations, all tests, one pass.

## Results

| | Rage XL PCI, Velocity9x | Rage IIC AGP, Velocity9x | Rage IIC AGP, ATI 4.11 | Rage Mobility-M (Gateway), Velocity9x | GMA 950 (netbook), Velocity9x |
|---|---|---|---|---|---|
| CPU | P3 1002 MHz | P3 1002 MHz | P3 1002 MHz | ~450 MHz | Atom |
| Half-Life mwd5 D3D, best | **18.146** (14.922, 18.134) | 5.561 | 6.627 | 13.44 | 42.24 |
| Quake 2 timedemo, OpenGL | **11.6** | 4.4 | does not start | not on file | 20.3-21.9 |
| 3DMark 99 / CPU | **1225** / 13700 | not run | 484 / 13614 | 604 / 7075 | 1253 / 14752 |

Sources:
- Rage IIC on Velocity9x: `2026-10-03-rage-iic-alpha-test.md` (Half-Life)
  and `-ati-driver-sampled-texture-cache.md` (Quake 2).
- Rage IIC on ATI's driver: `2026-10-02-rage-iic-native-baseline-a8u4i5.md`.
- Gateway: `2026-10-01-hl1-d3d-sky-was-texture-address-zero.md` and
  `2026-10-02-3dmark99-640-both-machines.md`.
- Netbook: `2026-10-01-intel-async-corruption-was-the-dword-head.md`,
  `2026-10-01-gl-vertex-path-profile-quake2-netbook.md` and the same
  3DMark record.

The machines differ in CPU, bus and memory, so only the two A8U4I5
columns share a host.

## 3DMark 99 per test

| Test | Rage XL PCI | Rage IIC, ATI | Gateway | netbook |
|---|---|---|---|---|
| Rasterizer (3DRasterMarks) | 339 | 94 | 302 | 653 |
| Game 1 - Race | 14.0 FPS | 5.3 | 8.5 | 25.9 |
| Game 2 - First Person | 10.9 FPS | 4.5 | 4.7 | 8.3 |
| Fill rate / multi-texturing | 37.6 / 37.8 MTexels/s | 7.7 / 7.7 | 35.0 / 35.3 | 72.8 / 73.0 |
| Texture rendering 2/4/8/16/32 MB | 51.5 / 46.7 / 7.6 / 4.3 / 2.3 FPS | 26.0 / 18.8 / 11.4 / 6.7 / 3.6 | 18.8 / 11.7 / 6.9 / 3.9 / 2.1 | 74.1 / 76.6 / 17.8 / 9.8 / 5.1 |
| Point / bilinear / trilinear | 113.8 / 100.0 / 66.7 % | 179.5 / 100 / 100.4 | 113.5 / 100.0 / 66.7 | 105.9 / 100.0 / 100.7 |
| Polygons 6 px, individual / strips | 250.3 / 282.7 KPolygons/s | 388.4 / 380.1 | 135.8 / 140.6 | 639.9 / 592.5 |
| 25 px | 235.8 / 284.0 | 160.0 / 156.1 | 135.4 / 141.4 | 633.8 / 557.0 |
| 50 px | 199.3 / 226.2 | 91.0 / 87.3 | 123.3 / 140.0 | 626.0 / 481.0 |
| 250 px | 78.2 / 78.2 | 22.5 / 21.0 | 71.1 / 71.3 | 92.3 / 70.6 |
| 1000 px | 24.8 / 25.0 | 6.2 / 6.0 | 22.5 / 22.9 | 15.7 / 14.8 |
| Refresh rate | VSync Off | 73 Hz | VSync Off | 59 Hz |
| Missing features | Sub-Pixel Accuracy | none | Sub-Pixel Accuracy | none |

3DMark offered 640x480, 800x600 and 1024x768 at 16 bits, with 8128 KB
total, 3750 KB used and 6592 KB of texture memory. Its texture format
was 16-bit 4444 RGBA.

## What the counters say

- **Half-Life:** 2,093,120 Mach64 draws and 21,186,203 triangles. 23,894
  were refused: 23,499 alpha test (reason 14), 357 texture shape
  (reason 8), and the probe's 36 alpha-force, one colour key and one
  format. The Gateway refused the same 357 shapes and about 13k alpha
  tests in the same demo. The Rage IIC's alpha-test work today
  (`eca481d`) is in its own policy, `rage2_draw.c`, not the Mach64's.
- **Quake 2:** the ICD drew 127,575 batches (4,680,882 triangles) on the
  engine. 7,584 batches (448,995 triangles) were refused by it
  (`hw-refused`); not looked into. 419,074 degenerate triangles.
- **3DMark:** 2,585,580 draws and 37,007,963 triangles over the session,
  13,181 texture creates, and **`FlipDeclined=61145`**. Every flip was
  declined and presented by copy, so "VSync Off" and the reporter's
  flicker are one cause
  (`docs/issues/2026-10-03-mach64-class-flips-declined-full-screen-flickers.md`).
- No timeouts, resets or abandonments in any run.

## Pictures

- Half-Life: drawn correctly in the one frame captured, a cave behind
  the console (`hl-running.png`).
- Quake 2: textured and lit correctly (`q2-running.png`).
- 3DMark: nobody watched the run.

## Not established

- Run-to-run spread: one 3DMark pass and one Quake 2 run.
- How the 7,584 Quake 2 batches the hardware path refused were drawn.
- Whether the Rage XL is faster than ATI's own driver on this card: ATI's
  driver was not benchmarked with the XL fitted.

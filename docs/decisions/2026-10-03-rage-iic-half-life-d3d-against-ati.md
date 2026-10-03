# Half-Life mwd5 on the Rage IIC: 4.29 fps against ATI's 6.63, and why

Date: 2026-10-03. Machine: A8U4I5 (Rage IIC AGP `1002:4757`, P3 1002 MHz,
Win98SE), Velocity9x at `f30e1d5`'s HAL and ABI 2026100303.
Evidence: `docs/probe/a8u4i5-rage-iic-registers-2026-10-02/hl-d3d-mwd5/`
(console log and screenshot, counter snapshots around the session) and
`phase6-clocks/` (`ATIRX /fill`, `/fillz`).

Michael asked for the comparison. Per the 2026-09-26 rule the figures
are recorded and explained, not chased.

## Procedure

As the native baseline's: Half-Life 1.1.1.0, `hl.exe -d3d -w 640 -h 480
-full -console -condebug`, one console session, `timedemo mwd5` three
times, best of three. The launcher stopped at its Video modes page the
first time (the Direct3D device it remembered was ATI's) and was taken
through it with OK. `config.cfg` was saved first and put back.

## Result

| | Run 1 | Run 2 | Run 3 | Best |
|---|---|---|---|---|
| ATI 4.11.2474 (boot 124) | 6.128 | 6.599 | 6.627 | **6.627** |
| ATI 4.11.2611 community | 6.184 | 6.598 | 6.629 | **6.629** |
| Velocity9x | 4.036 (cut at 379 of 393 frames) | 4.294 | 4.287 | **4.294** |

Velocity9x is 35 % slower. The picture is right (Xen terrain, models,
weapon; `hl-running.png`), on the hardware renderer (`hw build 2056`).
Run 1 was cut short by the next command arriving 4 s early and is not
counted.

Over the session: 210,709 batches, 2.77 M triangles, 1.06 G pixels (about
0.9 M a frame, three screens), draw 243 G TSC cycles of which emit 77 %
and FIFO reads 2.3 per register write: the engine is the limit, as in
Quake 2. **10,728 batches were refused and not drawn**: 10,371 for alpha
test (the chip tests only 1555's alpha bit) and 357 for texture format.
No flips were counted.

## Why ATI is faster

At ATI's 6.63 fps a frame takes 151 ms. Drawing our 0.9 M pixels in that
time needs under 14 engine clocks a pixel on average; Velocity9x's
cheapest bilinear-with-Z mode costs 17.1 and the lightmap pass 19.4
(`ATIRX /fill`). ATI's driver therefore makes the engine do less work
for the same frame. What the evidence shows:

- **Half-Life asks for mip-mapping and does not get it.** It sets the
  minification filter to MIPLINEAR (`FilterMinSeen=0x16`), but created no
  mip-mapped texture (`MipTreeAllocs=0`, `MipDraws=0`): the Rage IIC's
  caps advertise `PERSPECTIVE | POW2 | ALPHA` and filters `NEAREST |
  LINEAR` only, and the policy samples level 0 (`MIP_MAP_DISABLE`). ATI's
  driver had a descending mip chain in `TEX_0_OFF`-`TEX_10_OFF` at the
  register survey (boot 125), so under ATI Half-Life mip-maps.
- **What level 0 costs when minified** (`ATIRX /fill`, engine clocks a
  pixel): bilinear over a 256x256 map at 4 texels a pixel 18.8, the 64x64
  map at 1:1 that mip level 2 would be 12.2; with Z 23.3 against 17.2.
  Point sampling is 5.9 either way. Mip-mapping would cut a minified
  bilinear pixel by about a quarter, and the 4 MiB's texture load with it.

That accounts for part of the gap, not all of it: even with every world
pixel at its mip level the mix stays above 14 clocks. Not known, and the
next questions: whether ATI's driver filters minification bilinear at
all (the chip selects magnification and minification filters apart; a
point-sampled minification would cost 5.9), and how many pixels
Half-Life draws a frame under ATI's caps. Neither can be read without
booting ATI's driver.

## Hypotheses the evidence killed

- **Lower clocks under Velocity9x.** XCLK is 83.1 MHz, the BIOS's figure;
  MPLL_CNTL and MEM_CNTL read as under ATI's driver (`ATIRX /pll`).
- **Colour and Z buffer placement.** Z at eight distances from the colour
  buffer, 0 to 128 KiB, at Half-Life's 1280-byte pitch and at 512: flat
  with Z 48.5-49.7 M cycles, bilinear with Z 216.7-217.9 M, within 2 %
  (`ATIRX /fillz`). The Z cost is the chip's.
- **The CPU.** Setup is 23 % of the draw; the engine wait 77 %.

## Next

1. Measure the Rage II's mip-mapping in `ATIRX` (level selection, the
   `TEX_n_OFF` chain, the LOD rule), as Phase 4 measured filtering, then
   advertise the mip caps and build chains. Done the same day
   ([record](2026-10-03-rage-iic-mip-mapping.md)): it works, and it did
   not move mwd5 (4.291 fps), which disputes this record's reading that
   mip-mapping explains part of the gap.
2. The 10,371 alpha-tested batches Half-Life loses: a 4444 or 8888 alpha
   test the chip cannot express, which today draws nothing.

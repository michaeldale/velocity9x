# GT218: tiled Z and colour shadows need whole row blocks; 64 KiB pages were not enough

Date: 2026-11-16

Status: fixed and measured on W9QI0DAD (Quake 3 demo, boot 40; Quake 2
demo, boot 42). Installed on M-BXMFA8, where Daniel reported it, but
that machine did not come back from the restart; unverified there.

Supersedes the cause given in
[2026-11-16-gt218-tiled-64k-pages.md](2026-11-16-gt218-tiled-64k-pages.md).

Evidence: [`../probe/gt218-rblock-2026-11-16/`](../probe/gt218-rblock-2026-11-16/).

## The report

After the 64 KiB build was installed on M-BXMFA8 (boot 138), Daniel
answered (chat, 2026-11-15 22:41 UTC) that Quake 3's texture corruption
and Quake II's corner strips "remains on my system", and that Quake II's
went away after an alt-tab out and back. His Quake II shot (1024x768,
32 bpp, taken before the 64 KiB build) has the first rows at the top
left with only the blue byte changed, in a gradient down the rows, and
the last 8 rows at the bottom right replaced.

## Reproduced on W9QI0DAD

The earlier measurement ran Quake 3 at 640x480 on a 16 bpp desktop.
W9QI0DAD's `q3config.cfg`, as that session left it, had
`r_textureMode GL_LINEAR` and `r_vertexlight "1,linear:"` (vertex
lighting, no mipmaps). Daniel runs trilinear mipmaps and lightmaps at
800x600 and 1024x768, 32 bpp.

Frames with a run of 20 white pixels in the first 12 rows (Quake 3's own
F8 TGAs, the `topgarbage` measure of the 64 KiB record), 64 KiB build:

| Mode | Settings | Frames with garbage |
| --- | --- | --- |
| 800x600x32 fullscreen | trilinear, lightmaps | 8 of 8 |
| 800x600x32 fullscreen | trilinear, vertex light | 7 of 8 |
| 800x600x32 fullscreen | bilinear, lightmaps | 8 of 8 |
| 800x600x32 fullscreen | bilinear, vertex light | 8 of 8 |
| 1024x768x32 fullscreen | | 0 of 5 |
| 960x720x32, 640x480x32, 512x384x32 fullscreen | | 0 of 5 each |
| 800x600x16, 1024x768x16 fullscreen | | 0 of 5 each |
| 800x600 windowed, 32 bpp desktop (client 800x587) | | 0 of 5 |

So the texture filter and the lightmaps were not the cause. The garbage
is always rows 0-7 and x 0-303 (19 of the colour shadow's 16-pixel
blocks): white blocks, blue-only streaks, black, where the notify line
is drawn. A corridor floor streaked as in Daniel's Quake 3 shot in the
same run (`w9qi0dad-q3-800x600x32-before.png`).

## Measured

A measurement build of the HAL (`plog-measurement.patch`, not
committed) logged every placement and took raw dumps once the engine
was idle.

- Placement (`V9XN50P-fs800x32-excerpt.txt`): the back buffer is the
  HAL's own render target at 1D4C00h (pitch 3200, ending 3A9800h); the
  Z's heap block starts at 3AA480h; the Z sits at 3B0000h, its colour
  shadow at 4A0000h. Nothing else overlaps them: the textures start at
  68B500h, past the block. The clean windowed case has the Z and shadow
  at the same addresses (`V9XN50P-w800x587-excerpt.txt`), so an address
  alone is not the cause.
- Colour (`V9XN50M1.BIN`, `V9XN50M2.BIN`, decoded by `decode.py`): the
  raw shadow, de-swizzled, equals the back buffer byte for byte, garbage
  included. It is in the shadow, not added on the copy out.
- Z (`decodez.py`, `prez.py`): the Z's first 8-row band is FFFF with
  stray bits, and is the same in both dumps, so the engine never wrote
  it. The 26 KiB between the back buffer's end and the Z's base holds
  depth values, and 24,111 of its 26,624 bytes changed between the two
  dumps: the depth engine was writing below the Z. World draws in the
  top rows tested against the stale Z and failed, and the notify text,
  drawn without Z, blended on top frame after frame.

## The layout, measured with the channel probe

`V9XN5CH /lean /3d /z:tall` and `/z:map` (new stages, this commit): a
256x600 Z16, tile mode 10h, at a known offset in the probe's window,
marker words around it, the hardware clear to 1.0, then one rectangle
per 8-row band at its own depth. Each 512-byte block of memory is then
named by the band it holds (`V9XN5CH-zm-*.ini`, 16 KiB a `Map` key).

- Inside a 16 KiB piece the layout is ours: bands of 8-row,
  64-byte-wide blocks, left to right.
- The 16 KiB pieces themselves are reordered, by a rule that depends on
  the address. With the base at 140000h, bands 0-3 sit 16 KiB into the
  Z and bands 4-7 at its base. At 150000h and at 144000h, bands 0-3 sit
  below the base (32 KiB and 16 KiB). At 160000h and 170000h, nothing
  goes below. 592 rows moves the pieces exactly as 600 does.
- The clear writes whole 64 KiB pages past a Z's end
  (`V9XN5CH-ztall-592.ini`: 5,120 words past a 600-row Z).

Every run stays inside the 192 KiB blocks, counted from video memory 0,
that its Z touches. The probe's window starts 128 MiB in, so its
boundaries fall at window offsets 130000h, 160000h, 190000h and 1C0000h.
The Z at 140000h ends its data exactly at 190000h. The one at 150000h
starts its data at 148000h, above 130000h.

That is nouveau's row block. `nv50_fb_vram_rblock`
(nvkm/subdev/fb/ramnv50.c) computes partitions × banks × 2^column bits
× 8, and three times that when 100250h bit 0 is set. Its VRAM allocator
keeps allocations of different storage types in separate row blocks
(nvkm_mm `block_size`). The bringupkit trace of nouveau on this card
(`bringupkit-gt218-20261113/gt218-load.txt.gz`) has 1540h = F3010001
(one partition), 100204h = 0166A020 (2^10 columns, 8 banks) and
100250h = 1. That gives 196,608 bytes, the 192 KiB measured.

Quake 3 at 800x600 x 32 puts the Z at 3B0000h, 128 KiB into the row
block that starts at 390000h and holds the back buffer's last rows.
Every copy from the shadow into the back buffer overwrote the Z data
that had moved there. The colour shadow at 4A0000h also starts 128 KiB
into a row block (480000h) that holds the Z's tail. Which addresses
collide depends on the size, so 800x600 x 32 broke and the other modes
did not.

## Hypotheses this kills

- The cause given in the 64 KiB record (a kind laying out its own 64 KiB
  page). The 64 KiB build helped at 640x480 x 16 because it moved the
  surfaces, not because the row blocks were kept apart.
- Mipmapping and lightmaps (the table above).
- Placement overlap in the heap (the placement log).
- A copy-out fault (the decoded dumps).

## The change

- `nv50_rblock.c` / `nv50_rblock.h`, host-tested in `test_nv50_init.c`:
  the row block from the three registers, a unit that is a whole number
  of row blocks and of 64 KiB pages, and the placement of a Z and its
  colour shadow, each from a unit boundary in units of its own.
- `eng_nv50.c` `v9x_nv50_rblock_unit_now`: the registers read once
  (a new external symbol inside the HAL, declared in ddhal_internal.h).
- `d3d_nv50.c` `create_depth`: asks the heap for the pair rounded to
  units plus one unit, and places the Z and colour on unit boundaries.
  It declines the Z if the unit cannot be read.

Cost: the pair rounded up to 192 KiB units, plus up to 188 KiB of
alignment, once per Z. At 1024x768 both sizes are already whole units.

## Measured after (W9QI0DAD, boot 40)

| Mode | Frames with garbage |
| --- | --- |
| 800x600x32 fullscreen, two runs | 0 of 13 |
| 1024x768x32, 640x480x32, 800x600x16 fullscreen | 0 of 5 each |
| 800x600 windowed, 32 bpp | 0 of 5 |

No streaked floors in the frames, no draw refused
(`w9qi0dad-q3-800x600x32-after.png`).

The Quake 2 3.14 demo, installed on W9QI0DAD for this, at Daniel's
1024x768 x 32 fullscreen (`+map demo1`, boot 42): the top-left and
bottom-right corners, where his strips were, are clean in four of
Quake 2's own screenshots. Quake 2 was not run on the 64 KiB build, so
this is not a before-and-after comparison.

## M-BXMFA8

The boot-40 files, hash-identical, were staged by WININIT rename and the
machine restarted with `reset` (from boot 138). It had not reconnected
11 minutes later. Whether the new HAL or the warm reset is the cause is
unknown; a warm reset stopped it at the Windows logo once before
(2026-11-14). Daniel has been asked by chat to power-cycle it and
retest.

## Not yet seen

- M-BXMFA8, above. It is a different board (Gainward, 512 MiB), so its
  row block may differ; the driver reads it at run time.
- The rocket launcher's purple, on every build so far.
- Whether 3DMark 2000's sky and 3DMark 2001's alternate black frames
  were this.

## Gates

`build-host.ps1`, `check-tree.ps1` and `run-checks.ps1` green; the five
driver files hash-checked on W9QI0DAD after `reset` (boot 40).

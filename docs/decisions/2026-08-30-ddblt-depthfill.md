# DDBLT_DEPTHFILL is one solid fill, and the flag is not where guessing puts it

Date: 2026-08-30
Branch: `d3d-mode-selector`
Plan: [`s3-trio64-voodoo2-hybrid-3d.md`](../plans/s3-trio64-voodoo2-hybrid-3d.md),
mode 3's first deliverable - and worth doing whether or not mode 3 ever exists.

Guest: `Win86SE`, 86Box, `s3` family, ViRGE/DX 86C375, 4 MiB, 1024x768x16,
agent port 9869. Build `5d69a51-dirty`.

## What it cost before

Nothing in `src/` served `DDBLT_DEPTHFILL`, and `DDCAPS_BLTDEPTHFILL` was not
advertised, so DirectDraw cleared a Z buffer by locking it and writing every
word from the CPU through the uncached aperture, once per frame. That is part
of what Final Reality's 25-pixel figure paid when depth testing began actually
happening - 28.54 to 23.62 Kpolys/s
([2026-08-30](2026-08-30-virge-depth-fifo-reservation.md)).

## The implementation is smaller than it looks

A depth clear is a solid fill of a 16-bit surface. `v9x_virge_fill` is already
parameterised on the destination offset, the destination surface's own pitch
and the bytes per pixel, and takes its value from `bltFX.dwFillColor` - which
is a union member sharing its DWORD with `dwFillDepth` (`DDRAW.H:223-229`). So
**no engine code changed at all.** `v9x_depthfill_body` in `ddhal_core.c`
validates the request and hands the existing fill a different offset and a
different width.

Three things it does not do, each deliberate:

- **It does not consult the screen depth.** The colour fill gates on
  `v9x_depth_is_blittable(fb.bits_per_pixel)`; a depth surface is 16-bit
  whatever the desktop is, and applying that gate here would refuse a
  legitimate depth clear on an 8- or 32-bpp desktop for a reason unrelated to
  the surface being filled.
- **It does not hardcode 2 bytes per pixel.** `ddhal_core.c` is chip-neutral
  and the depth width is the engine's, so it asks
  `v9x_d3d_depth_bytes_per_pixel()`, which reads `depth_bits_per_pixel` out of
  the fitted engine's limits. A literal 2 in that file is exactly the mistake
  the comment on that field already records having made once. Zero means the
  chip has no D3D engine, and the fill declines - so a card that cannot have
  depth buffers cannot be asked to clear one.
- **It does not accept any destination.** `DDSCAPS_ZBUFFER` is required and
  `DDSCAPS_SYSTEMMEMORY` refused. A depth fill aimed at the primary would
  otherwise paint the screen with a Z value.

## The constant, which memory gets wrong

`DDBLT_DEPTHFILL` is **`0x02000000`**, from `C:\98DDK\src\display\inc\DDRAW.H`
line 2799. Its neighbours in the flag list suggest `0x00002000` - `DDBLT_DDFX`
is `0x00000800`, `DDBLT_DDROPS` `0x00001000` - and that is what writing it from
memory produces. The `DDBLT_` flags are not densely packed and this one sits
above `DDBLT_ROP`. `DDCAPS_BLTDEPTHFILL` is `0x10000000` (`DDRAW.H:1745`).

Both were read out of the DDK before either was used. That is the whole reason
to check: the wrong flag value would have made the driver decline every depth
fill while claiming a capability it never received a request for, and the
symptom would have been "no change in performance" rather than anything that
looks like a bug.

## Measured

`V9XDDP.EXE` gained `v9x_probe_depth_fill`, which fills a 64x64 16-bit
video-memory Z surface twice and reads the words back:

```
ZDepthFillHr=0x00000000    ZDepthFillRaw=4660        (0x1234, first fill)
ZDepthFill2Hr=0x00000000   ZDepthFill2Raw=43981      (0xABCD, second fill)
                           ZDepthFillCornerRaw=43981 (0xABCD at 63,63)
ZDepthFillOk=1
```

**Two values, not one.** Freshly allocated video memory holds whatever the last
owner left, so a single-value test passes by accident often enough to be
worthless; the second fill has to change what the first one wrote. **Two
positions**, because a rectangle blit with the wrong pitch or height writes the
first word or the first row and nothing else, and reading `(0,0)` alone cannot
see it.

The rest of the ladder is unchanged on the same run: `D3DZCompareOk=1`,
`D3DZWriteMaskOk=1`, `TexFormatCount=2`, `D3DHalFound=1`, `Result=COMPLETE`,
`Build=` matched, result file deleted first, `exec` exit code 0, one run in the
boot.

## The caps change is inert on a family without Direct3D

`DDCAPS_BLTDEPTHFILL` is added to the word DriverInit publishes for the whole
binary, so every family's HAL now claims it before the 16-bit side narrows
anything. That is a regression risk on three families, and it was checked
rather than reasoned about.

`Win98SE-Trio64`, same build, new HAL installed: `D3DHalFound=0`,
`D3DDeviceCount=3`, `TexFormatCount=0` - the clamp in
`V9xDdCreateDriverObject` reassigns `dwCaps` to `GDI | BLT | BLTCOLORFILL` and
therefore drops the new bit with the rest. DirectDraw stayed fully working:
`BltFillPixelOk=1`, `SrcCopyPixelOk=1`, all four overlap cases,
`RestoreHr=0x00000000`, `Result=COMPLETE`. `FlipPixelOk=0` is the known
pre-existing result.

The `ZDepthFill*` keys are absent from that run, correctly: the probe's depth
test lives inside the D3D block, and a chip with no Direct3D device never
creates a Z surface to fill.

## What is not established

- **Which path served it.** `v9x_depthfill_body` tries the engine and falls
  back to `v9x_cpu_fill`, and both produce the same words, so the pixel test
  cannot tell them apart. The engine was live and validated on that boot
  (`D3DZCompareOk=1` requires it) and `v9x_virge_fill` declines only above 2
  bytes per pixel, so the engine is the strong inference - but it is an
  inference. `V9X_TRACE_BLT_ENGINE` is the counter that would settle it.
- **Whether it is faster.** No before/after benchmark was taken. The argument
  for the change is one blit per frame against one CPU pass per frame over the
  aperture, which is structural, but Final Reality has not been re-run.
- **Any chip but the ViRGE.** No other family has a D3D engine, so
  `v9x_d3d_depth_bytes_per_pixel()` returns zero everywhere else and the fill
  declines. That is asserted by construction, not measured.

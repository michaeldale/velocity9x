# The ViRGE needs a dummy blit after a 3D command, and this driver does not issue one

Filed: 2026-08-30
Status: open, unreproduced here
Found in: `98DDK\src\display\mini\s3v\S3_DD32.C:860-874`, while looking for
best-practice precedent on `DDBLT_DEPTHFILL`.

## What S3's driver does

Every blit in the DDK's ViRGE sample is prefixed by a 1x1 screen-to-screen
BitBLT - seven FIFO slots and seven register writes - under the comment:

```
//### patch: do a dummy screen to screen blt in case we were called right
//###        after a 3D command
```

`vir_wait_for_fifo(7)`, then `VI_SRC_BASE`, `VI_DEST_BASE`,
`VI_DEST_SRC_STR`, `VI_RWIDTH_HEIGHT`, `VI_RSRC_XY`, `VI_RDEST_XY` and a
`VI_CMD_BITBLT` with ROP `0xCC` over a single pixel at the origin.

It is unconditional: colour fill, depth fill and source copy all pay it.

## Why it matters here

`v9x_depthfill_body` and `v9x_colorfill_body` issue their 2D command with no
such prefix. For a depth clear that is exactly the case the patch names - the
clear is issued between frames, immediately after the S3D engine has been
drawing - so this driver runs the sequence S3 worked around.

Nothing has gone wrong. `DDBLT_DEPTHFILL` is pixel-verified twice over on
`Win86SE` and Final Reality drives millions of triangles with clears
interleaved and reports no FIFO timeout, idle timeout or engine reset. But
that is one emulated chip, and 86Box need not model an erratum the vendor
patched around in software.

## What is unknown

- Whether the defect is real on the silicon, and what its symptom is. The
  comment says "in case", which reads like defensive coding against an
  observed fault rather than a documented erratum, but S3 wrote it about their
  own part.
- Whether 86Box models it at all. If it does not, no amount of guest testing
  here can find it.
- Whether the GDI acceleration path in `src\display16\gdi_accel.c` has the
  same exposure. It issues 2D fills and copies from the display driver, which
  can also run just after Direct3D work on the ViRGE.

## Why it is not simply copied

Seven FIFO slots and seven register writes on every blit is a real cost -
`docs\decisions\2026-08-30-ddblt-depthfill.md` measures the depth clear
itself as costing 38% of Final Reality's fill rate on this emulator, and the
prefix would add to that. Adding an unconditional workaround for a fault
nobody here has seen, on the strength of one comment, is the kind of change
this project asks for evidence before making.

The cheap first step is to find out whether it is needed at all: run the
existing Direct3D pixel ladder with a 2D fill deliberately issued between
every draw, with and without the prefix, and see whether anything differs.
If 86Box shows nothing either way, the question waits for physical ViRGE
silicon, which this project does not currently have.

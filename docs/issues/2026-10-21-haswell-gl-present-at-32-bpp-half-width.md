# Haswell, OpenGL at 32 bpp: SwapBuffers leaves the right half of the window stale, and the front buffer reads black

Date: 2026-10-21. Status: open. Found by V9XGLP; the cause has not been
looked for.

Machine: the i5-4570 (HD 4600, 10.0.1.254), boot 60, driver
`f8318ee3-dirty` (the ICD's 32 bpp colour, Phase 4 Step 1 of
[`../plans/32bpp-acceleration-and-3d.md`](../plans/32bpp-acceleration-and-3d.md),
which had not run on any machine before). Evidence:
[`../probe/hsw-4570-icd-z24-2026-10-21/`](../probe/hsw-4570-icd-z24-2026-10-21/).
V9XGLP still reports `Result=PASS`: it reports these keys and does not
judge them.

## What was seen

V9XGLP's window is 400x300. The keys compared are GDI's `GetPixel` on the
window after `SwapBuffers`, and glReadPixels of the back and front
buffers:

| Key | 32 bpp, format 1 | 32 bpp, format 2 | 16 bpp, format 1 | 16 bpp, format 2 |
|---|---|---|---|---|
| GeometryLeftPixel (GDI, x 100) | 0x0000FF00 | 0x0000FF00 | 0x0000FF00 | 0x0000FF00 |
| GeometryRightPixel (GDI, x 300) | **0x00FF00FF** | **0x00FF00FF** | 0x000000FF | 0x000000FF |
| ReadGeometryRight (back buffer) | 0x00FF0000 | 0x00FF0000 | 0x00FF0000 | 0x00FF0000 |
| FrontReadFront | **0x00000000** | **0x00000000** | 0x00FFFF00 | 0x00FFFF00 |

- The back buffer holds the right image at 32 bpp: green left, red
  right, as at 16 bpp.
- After the swap, the screen's left quarter is right. Its right quarter
  still shows magenta (0x00FF00FF as a COLORREF), the colour of the
  frame swapped before. The right half of the window was not copied.
- With glDrawBuffer(GL_FRONT), a yellow quad over the lower-left quarter
  does not read back from the front buffer at 32 bpp. At 16 bpp it does.
- The depth format plays no part: formats 1 and 2 agree. The ICD logged
  no failed Blt (`present hr=` or `front:` lines) in either run.

## Hypotheses, none tested

1. A HAL Blt between an offscreen `DDSCAPS_3DDEVICE` surface and the
   primary computes its width in 2-byte pixels at 32 bpp, so it copies
   half the row. That would fit both symptoms: the present copies the
   left half, and the front buffer's copy from the screen is cut short
   too. The BCS fill and copy measured at 32 bpp on 2026-10-17
   ([`../decisions/2026-10-17-haswell-phase-b2-directdraw-on-bcs.md`](../decisions/2026-10-17-haswell-phase-b2-directdraw-on-bcs.md))
   ran within one 64x16 off-screen surface. They did not cover a copy to
   the primary, so they do not rule this out.
2. Something else in the clipped present path, which DirectDraw splits
   into one HAL Blt per visible rectangle.

## Next

A screenshot of V9XGLP's window at 32 bpp. Then V9XDDP's offscreen-to-
primary blit cases at 32 bpp, which would separate the HAL Blt from the
ICD. The same probe on the netbook (Gen3 BLT) would show whether this is
the BCS path or shared code.

## Cause and fix (2026-10-21 to 2026-10-22, boots 60 to 62)

Evidence:
[`../probe/hsw-4570-bcs-flush-2026-10-22/`](../probe/hsw-4570-bcs-flush-2026-10-22/).
V9XGLP gained reads made for this: the window's pixels in video memory
through a DirectDraw lock of the primary (`VramGeometryX0`-`X7`,
`FrontVramScreen`); the same GetPixel 200 ms later; a front draw made
200 ms after the swap (`FrontReadFrontSettled`); and a second front read.

**Neither hypothesis above was right.** Nothing is copied at the wrong
width.

1. **The screen holds the whole frame (boot 60).** Video memory read
   green across the left half and red across the right, matching the
   back buffer. Only GDI's read disagreed. GetPixel at the same point
   200 ms later read red (`GeometryRightPixelLate=0x000000FF`). With
   200 ms between the swap and the front draw, the front buffer read
   yellow (`FrontReadFrontSettled=0x00FFFF00`). Both symptoms are
   ordering, not geometry.
2. **A wait for the blitter before GDI was not enough (boot 61).** Blits
   into the visible primary now wait for the engine before Blt returns
   while GDI may draw (`v9x_blt_settle_on_screen` in `ddhal_core.c`, plan
   decision 3's rule for draws, now applied to blits). The symptoms
   stayed. The front draw returned OK (V9XGL.LOG: `front: made 400x300,
   copy from screen hr=00000000`), and the HAL counted no refusals and no
   idle timeouts (`V9XSNA7-boot61.INI`). Yet the front read black twice
   in a row, and so did video memory under it. The draw path already
   waits for the blitter (`d3d_hsw.c`). So the blitter was reported idle
   while the front buffer's copy from the screen was still being
   written, and the tail of that copy landed over the yellow draw.
3. **The blitter's completion store did not flush.** `eng_hsw.c` ended
   each blit with MI_STORE_DATA_IMM of its sequence number. Vol 2b's
   MI_FLUSH_DW (BlitterCS) is the command that pauses the parser "until
   all drawing engines have completed any pending operations" and
   flushes dirty data, and: "After this command is completed with a
   Store DWord enabled, CPU access to graphics memory will be coherent."
   A plain store promises neither. The sequence is now written by
   MI_FLUSH_DW's post-sync write (`v9x_intel_hsw_mi_flush_dw_store`,
   host-tested, failing to build before the builder). It is the same
   four dwords, so the ring arithmetic is unchanged.

**Boot 62, both changes:** V9XGLP at 32 bpp three times, 16 bpp once,
and format 2 at 32 bpp, all with `Result=PASS` and `LifetimeFailures=0`:

| Key | before (boot 60) | boot 62 |
|---|---|---|
| GeometryRightPixel | 0x00FF00FF | 0x000000FF |
| FrontReadFront | 0x00000000 | 0x00FFFF00 |
| FrontDrawScreen (GDI) | 0x00000000 | 0x0000FFFF |

`FrontDrawScreen` was 0 at 16 bpp too, before this. So the same early
"idle" was already losing the front draw's appearance on screen at
16 bpp. V9XDDF's fill and copy pass at 32 and 16 bpp with the values
of 2026-10-17 (`0012A5C3`/`00ED5A3C`, `07E0`/`F81F`, 0 mismatches).

What is not known: whether the settle wait is needed once the flush is
in. Only both together were run. It stays, because it is plan decision
3's rule and a blit can still be in flight when Blt returns. The Ivy
Bridge shares `eng_hsw.c` and has the change, unmeasured there. Gen3
already flushes (MI_FLUSH before its store in `i9xx_blt.c`).

Status: fixed on the HD 4600, boot 62.

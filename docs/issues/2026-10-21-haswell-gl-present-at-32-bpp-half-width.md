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

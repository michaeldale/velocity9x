# RIVA 128ZX: 2D at 16 bpp stays on the CPU, because the engine's bit 15 is visible to software

Date: 2026-11-07. Machine: M-7V636C (remote help), RIVA 128ZX
(12D2:0018, 8 MiB), Windows 98 first edition, desktop 800x600x16 at
5:5:5 (DAC switched by `nvidia_hw16.c`, boot 26). Plan:
`docs/plans/nvidia-riva128.md`, decision 1 and phase 6. Evidence:
`docs/probe/m7v636c-riva128-16bpp-2d-2026-11-07/`.

## The question

At 16 bpp the 2D engine sets bit 15 of every pixel it writes, fills and
blits alike (`docs/decisions/2026-11-02-riva128-2d-engine-writes.md`).
DirectDraw and GDI acceleration stayed off at 16 bpp until the desktop
ran 5:5:5, where bit 15 is unused. Since boot 26 it does. Option B of
decision 1 assumed that an unused bit is a harmless bit.

## Boot 45: the engine on at 16 bpp

The build, never committed, did two things:

- It let `v9x_nv3_depth_drawn` draw 16 bpp when the desktop is X1R5G5B5.
  For GDI that means `v9x_modes16_highcolor()` is 5:5:5; for the HAL, the
  display format's masks.
- In the HAL it declined Z buffers and surfaces of any other format.

DRV `3FB02963...`, HAL `41A8B6B6...`, installed by WININIT rename and
`reset`, with both hashes checked.

- **V9XDDP:** 76 of 99 `*Ok` checks pass, against 82 of 98 on boot 44.
  - BltFill, SrcCopy and the four Overlap checks fail. They compare exact
    16-bit values, and the engine wrote bit 15.
  - The new check KeySrcFillPixelOk also fails. It fills a video-memory
    stage with the key 001Fh through `Blt` COLORFILL, then does a
    source-keyed blit over a red back buffer:
    - The stage read 801Fh.
    - DirectDraw's emulated keyed blit (this driver does not take
      colour-keyed blits) compares all 16 bits. So it copied the stage:
      the back buffer read 801Fh where the key should have left F800h.
- **V9XDDF:** a fill of 07E0h read back 87E0h, 1,024 mismatches.
- **V9XGDI `/accel`:** the engine fired: 45 fills and 8 copies, enabled
  primitives 7. Its comparison failed with 194,463 bytes different
  (`pixel-mismatch`). It does not record bit values, so bit 15 being the
  cause is likely, not shown.
- **The screen** looked right (`b45-desktop-after-gdi.png`), since the DAC
  ignores bit 15.
- **Half-Life `c1a0` windowed** drew normally. Its window is presented
  with a back-buffer blit, now on the engine; it showed 7.9 fps against
  4.7-5.6 on boot 44. The score is recorded, not compared.

## Can the engine be told to leave bit 15 alone?

`V9XN32D /alpha` (new): every object's grobj ALPHA bit (3) set, so format
0 reads as A1R5G5B5 instead of X1R5G5B5. Raw readback of the scratch
window, boot 45 (`b45-V9XN32D-alpha.INI`):

| Variant | Fill 7C1Fh | Fill F81Fh | Blit of 2483h |
|---|---|---|---|
| rivafb (X1R5G5B5) | FC1Fh | F81Fh | A483h |
| ALPHA set | not drawn (pattern left) | F81Fh | A483h |
| Y16 (boot 11) | C210h | FFFFh | wrong |

With ALPHA set, bit 15 of the fill colour acts as a blend factor: alpha 0
draws nothing. The blit still sets bit 15. The only other register
mechanism envytools lists, the PLANE object's plane mask, is marked in
`nv3_pgraph.xml` (grobj option bit 14) as "does not actually work". It
was not tried.

## Boot 46: control, the committed build (c11dfd78)

DRV `C8CB85A3...`, HAL `C64B14DE...`, both hashes checked:

- V9XDDP: 83 of 99 checks pass, including KeySrcFillPixelOk. The stage
  read 001Fh, and the back buffer kept F800h.
- V9XDDF PASS (07E0h).
- V9XGDI `/accel` PASS, with acceleration off at 16 bpp, before and
  after Half-Life.

So every failure on boot 45 came from the engine writing at 16 bpp.

## Decision

16 bpp 2D stays on the CPU. Bit 15 is unused only by the display.
DirectDraw's colour-key emulation, programs that Lock and compare
pixels, and (by the likely reading) GDI's own readbacks all see it.

Not tried, and each would be its own piece of work:

- **Taking colour-keyed blits in the driver**, comparing without bit 15.
  This fixes the emulated key only. Programs that read pixels stay
  exposed.
- **The PLANE object**, against envytools' note.

## Kept

The two probe additions:

- V9XDDP's KeySrcFill keys. They test any driver whose fill writes bits
  the format leaves unused.
- `V9XN32D /alpha`.

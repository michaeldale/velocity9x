# Size the matrox family's memory by walking it: the 2164W gets its 8 MiB

Date: 2026-10-10. A8U4I5 (10.0.1.172), boot 385, the Millennium II
MGA-2164W whose VBE BIOS reports 4 MiB of at least 8
([issue](../issues/2026-10-10-mga2164w-vbe-reports-half-its-memory.md)).
Phase 0 of the [hardware Direct3D plan](../plans/matrox-mga2164w-hardware-3d.md).
Evidence in `docs/probe/a8u4i5-mga2164w-vram-walk-2026-10-10/`.

## Why a walk

The 2164W has no memory-size register (its specification offers only
OPTION.productid, which board makers "could" use), and the family's one
size source was 4F00h. `V9XVRAM.EXE` had already shown the method works
on this card (`Result=AT-LEAST-8MIB` under Matrox's driver, boot 383).

## Shape

- `src/common/vram_probe.c`, host-tested: the plan (a signature every
  512 KiB plus 200h, up to 32 points in the mapped window), the size a
  readback shows (the leading run of held points), and the acceptance
  rule: the walk's figure wins when it is at least the BIOS's and within
  the window; a smaller one is taken as a failed walk and the BIOS figure
  stays.
- Two primitives in `runtime.asm`'s matrox section: `V9xMgaScreenExchange`
  (a locked XCHG, storing a signature and returning the original in one
  operation, so no write-combined store hides behind the readback) and
  `V9xMgaScreenRead`, both through the framebuffer selector and incapable
  of choosing an offset.
- A hook appended to `V9X_HW16_OPS`, `measure_video_memory`, called by
  `enable16.c` on every Enable after `V9XMAPAPERTURE` maps the 16 MiB
  window, because the tier-0 path recomputes the usable size from 4F00h at
  every mode set. The matrox implementation walks once (first Enable,
  before anything is drawn), highest point first, and restores the
  originals in the same order. Zero in every other family.
- `V9XHW.INI` gains `VramMeasuredBytes`; `VbeVramBytes` stays the BIOS's
  figure.

## Result on the card

    VbeVramBytes=4194304
    VramMeasuredBytes=8388608
    Vram=reported=4194304 usable=8388608          (V9XMODES.INI)
    VddReserve=vdd=1920000 visible=1920000 vram=8388608

- V9XDDP at 800x600x32: `GblHalVidMemTotal=0x0062B400`, which is 8 MiB
  less the 1,920,000-byte screen exactly. Fill and the four overlap
  copies pixel-correct.
- `V9XGDI /accel` PASS at 800x600x32 (218 fills, 132 copies, 84 text, no
  FIFO or sync timeouts); the desktop is intact after the walk.
- `MGA2D /vram:8`, its region at 7 MiB: the seven fill and copy cases
  PASS, and with `/tri` all eight trapezoid cases match the model (the
  three Gouraud ones under the folded hypothesis, as at 3 MiB). The upper
  4 MiB is memory the CPU and the engine both reach. The probe's `/vram:N`
  now overrides the BIOS figure in either direction, capped by the
  framebuffer range; before, it could only lower it.

## Not settled

- The card is 8 MiB, not more: the walk covered the whole 16 MiB window
  and the point at 8 MiB did not keep its signature. Whether the upper half
  of the window wraps or floats is not recorded (the hook keeps only the
  size).
- The published mode list is unchanged at 14 rows. Whether the boot-time
  merge drops any BIOS mode for memory, and against which figure, was not
  checked.
- The 2064W runs the same walk; not yet booted with it.

# SiS 6326 3D engine stalls on Final Reality's first Z-tested batch

Date: 2026-10-05. Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh). Status:
open. Evidence: `docs/probe/a8u4i5-sis6326-fr-stall-2026-10-05/`. Plan:
[sis-6326-hardware-3d.md](../plans/sis-6326-hardware-3d.md), phase 5.

## Symptom

Final Reality 1.01's full benchmark, Direct3D on-board accelerator,
640x480x16. The 3D engine stops reporting idle (89FCh `00200074h`) inside
the first batch that tests Z. The driver's timeout path works as designed:
the engine is invalidated, the batch is logged to `V9XSIS3D.TXT`, and the
machine, desktop and agent stay up. Every later 3D draw is dropped and
every flip declined, so the screen goes black, and the benchmark finishes
with numbers that are not the chip's (fill rate 785 Mpixels/s, boot 231).
A warm restart recovers the engine.

The batch: 64 Gouraud triangles, a strip of 10-pixel quads at rows 80-85,
Z test and write (LEQUAL, Z16), every Z at the driver's clamp
`3F7FFFFFh`, a 256x256 RGB565 texture, bilinear, wrap, perspective, no
blend. Its texture lies directly after its Z buffer.

## Measured

- **It reproduces without Final Reality.** SIS3D `/phase6 /file` replays
  the logged batch from a host-generated stream and stalls at the same
  triangle as the driver (boot 259). Triangle 11 alone, from fresh state,
  stalls and stays busy through 120 further one-million-read waits (boots
  260, 264).
- **Z test is required.** The whole batch goes idle with Z test and write
  off (boot 261). With Z test on and Z write off, the first triangle still
  stalls (boot 255).
- **What does not help on the real batch:** a Z of 0.5 or 0.99 instead of
  the clamp; the texture moved 2 or 64 KiB away from the Z buffer; the Z
  buffer moved 2 KiB; SiS's Turbo Queue split (SR3C 43h, queue still off);
  100 status reads before each triangle (boots 262-269).
- **Single-triangle variants did not predict the batch.** On the first
  triangle alone, nearest filtering, clamp addressing, a texture 128 high,
  and moving the triangle one column right each went idle, while many other
  changes still stalled (boots 232-255; table in the evidence README). A
  driver drawing tall textures under Z with nearest stalled at triangle 9
  of the same batch, and with clamp at triangle 11 (boots 256-258). Neither
  workaround was kept.

## Hypotheses the evidence killed

- **A negative vertex coordinate or the left clip.** The frame shifted by
  the bases, with and without the clip crossing, still stalled; triangle 11
  lies at x 50-60.
- **The texture wrap seam.** Coordinates moved off both seams still
  stalled.
- **Texture-cache and enable details:** the D4 clear pulse, the large cache
  with bit 15, perspective.
- **Texture or Z placement**, at the 2 and 64 KiB moves tried.
- **The clamped Z value.**
- **Slowness rather than a hang** for triangle 11. The `delay` replay's
  final status read idle after its timeout, which is not explained.

## Not established

- What triangle 11 has that the ten before it lack. Its texture
  coordinates, colours and primitive word were not varied.
- How SiS's own driver draws the same scene. Its HAL never ran V9XDDP's Z
  tests on this card (boot 207), so whether it uses Z here at all is
  unknown.
- Boot 237 locked the machine (ping only) while SIS3D `/phase6 /ff` (left
  edge at X = 0) ran; the output never reached the disk, so the run is not
  proven to be the cause.

## Next

Run Final Reality under SiS's 2.28 driver on the same card and capture its
3D registers mid-run.

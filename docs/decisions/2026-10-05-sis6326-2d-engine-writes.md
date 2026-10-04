# SiS 6326 2D engine: first writes, and the two encoding questions settled

Date: 2026-10-05. Machine: A8U4I5 boot 208, SiS 6326 card 2 (rev 0Bh),
Velocity9x `sis` tier-0. Evidence:
`docs/probe/a8u4i5-sis6326-2d-writes-2026-10-05/`. Plan:
[sis-6326-family.md](../plans/sis-6326-family.md), Phase 2.

## What was run

`SIS2D.EXE` with `SIS2D.VXD`. The engine commands come from the
host-tested builder (`src/chipsets/sis/sis6326_engine.c`), whose tests
include reproducing SiS's own measured Start-button fill word for word.

The sequence:

1. Read SR05, SRB and SR27, unlocking with SR05 = 86h to do so.
2. Set SRB D[6:5] = 11 (MMIO through BAR1) and SR27 = engine registers on,
   Turbo Queue off, other bits kept.
3. Run four engine operations into off-screen VRAM at 2 MiB and above. Each
   one: the builder's dwords, then a 16-bit command write at 82AAh, a
   posted read of 82A8h, and a bounded wait on 82ABh D6.
4. Read every test region back through the framebuffer.
5. Write SR27, SRB and the lock back as found.

It was run at 640x480x8 and again at 640x480x16.

## Measured

State as Velocity9x tier-0 leaves it (the BIOS's): SR05 = 21h (locked),
SRB = 0Ch (MMIO off), SR27 = 00h. After enabling: SRB = 6Ch, SR27 = 40h,
82A8h = 80340020h at 8 bpp and 80120020h at 16 bpp: queue empty, not busy.
At 8 bpp the wait at enable cleared on its first read.

| Test | 8 bpp | 16 bpp |
|---|---|---|
| Fill, 32 bytes x 4 rows | PASS; changed bytes 16-47, rows 2-5 | PASS; same extent, colour 1234h as bytes 34 12 |
| Forward copy, separate surfaces (command 0032h) | PASS | PASS |
| Same surface, destination right on the same rows (command 0022h, right-to-left) | PASS | PASS |
| Same surface, destination lower (command 0012h, bottom-up) | PASS | PASS |

Every region matched the builder's model byte for byte, inside a guard
band that would have caught a one-row or one-byte overrun. The busy bit
cleared within 2-8 reads after each command. SR05 read back 21h (locked)
after the restore, both runs.

## What this settles

1. **Width and height are programmed as n - 1**, width in bytes. The fill
   register held 31 and 3 and the engine wrote exactly 32 bytes by 4 rows.
   This confirms the inference from SiS's Start-button fill
   (2026-10-04-sis6326-first-survey.md).
2. **A right-to-left copy starts on the last byte of its last pixel**, as
   xf86-video-sis does, at 16 bpp as well as 8.
3. **Bottom-up copies start on the last row** with Y decreasing, and
   forward copies with both directions increasing land exactly.
4. **The engine works with the Turbo Queue off and the engine-register bit
   alone set in SR27**, with MMIO through BAR1, from the state the BIOS
   leaves after a VBE mode set. The driver must enable all of it itself:
   nothing is on after the mode set.
5. **The fill SiS's driver uses works at 16 bpp too** (colour in the
   background register, source background, SRCCOPY in the foreground ROP).

## Not established

- Whether SRB and SR27 survive a mode change or a DOS box. The driver will
  re-enable after every mode set rather than rely on it.
- 24 bpp: not run (not published by the family).
- Clipping, the line engine, colour expansion: not used.
- Timing under load: each operation was synchronous.

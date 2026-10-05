# SiS 6326 colour blend factors: Direct3D's, on the datasheet's sides

Date: 2026-10-05. Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), boots 293
and 294. Evidence: `docs/probe/a8u4i5-sis6326-colour-blend-2026-10-05/`.
Plan: [sis-6326-hardware-3d.md](../plans/sis-6326-hardware-3d.md), phase 5.
Follows [shading and depth](2026-10-05-sis6326-3d-shading-and-depth.md),
which measured ZERO, ONE, SRCALPHA and INVSRCALPHA.

## Why

Half-Life's Direct3D path draws 2x modulate (source DESTCOLOR, destination
SRCCOLOR). The mapping refused it as unmeasured: 37,303 batches in three
mwd5 timedemos were dropped (boot 291). V9XDDP's BlendMultiply
(DESTCOLOR/ZERO) was refused the same way.

## Measured: SIS3D /phase7 (boot 293)

Each case:

1. fills a 64x64 RGB565 target with D;
2. draws a triangle of colour S through the driver's mapping, with the
   blend pair forced past its refusal, and TEND after the triangle;
3. reads one pixel and compares it with Direct3D's
   saturate(S x Fs + D x Fd), D expanded from 565 by bit replication.

| Case (source / destination) | S | D (565) | Read | Expected | |
|---|---|---|---|---|---|
| ONE / ZERO | 80C040 | 651E | 8608 | 8608 | match |
| DESTCOLOR / ZERO | 80C040 | 651E | 33C7 | 33C7 | match |
| ZERO / SRCCOLOR | 80C040 | 651E | 33C7 | 33C7 | match |
| DESTCOLOR / SRCCOLOR | 80C040 | 651E | 678F | 67AF | one step (green) |
| DESTCOLOR / SRCCOLOR, saturating | E0E0E0 | F79E | FFFF | FFFF | match |
| DESTCOLOR / SRCCOLOR, dark | 204060 | 4310 | 118C | 118C | match |
| INVDESTCOLOR / ZERO | 80C040 | 651E | 4A20 | 4A20 | match |
| ZERO / INVSRCCOLOR | 80C040 | 651E | 3157 | 3157 | match |
| DESTCOLOR / ONE | 80C040 | 651E | 97FF | 97FF | match |
| ONE / SRCCOLOR | 80C040 | 651E | B7EF | B7EF | match |
| DESTCOLOR / SRCCOLOR, textured (texel green) | 00FF00 | A40C | 07E0 | 07E0 | match |
| DESTCOLOR / ZERO, textured | 00FF00 | A40C | 0400 | 0400 | match |

Every draw went idle.

## Measured: the driver taking them (boot 294)

- **V9XDDP** completed with no idle timeout.
  - `BlendMultiplyOk` reads 1. It was 0 at boot 291, when the refused draw
    left the destination `F81Fh`; now it reads `0000h`, magenta times
    green.
  - The 121 compared checks are unchanged against boot 291 and against
    SiS's HAL.
  - Refusals fell from 5 to 2: one colour key, one texture format.
- **Half-Life, `timedemo mwd5`, Direct3D, 640x480:**
  - 7.905, 8.635 and 8.632 fps;
  - no idle timeout, no declined flip, no blend refusal.

  At boot 291 it ran at 10.4-11.4 fps with the 2x-modulate batches dropped.
  The difference is those draws, now made. Scores are recorded and not
  compared beyond that.

## Decision

The mapping accepts the colour factors on the side the datasheet gives
them a code (DS L5067-5111): DESTCOLOR and INVDESTCOLOR as source, SRCCOLOR
and INVSRCCOLOR as destination. The other side's colour factor, the
destination-alpha factors (there is no alpha buffer at 16 bpp) and
SRCALPHASAT stay refused. The device caps publish the same split.
Host-tested.

The comment that blamed a hard lock on a DESTCOLOR draw (boot 212) is
corrected. That lock was the untextured-batch stall and the Lock loop on
it ([engine record](2026-10-05-sis6326-d3d-engine.md)).

## Not established

- The rounding behind the one-step difference.
- BOTHSRCALPHA, BOTHINVSRCALPHA and the engine's SRC_ALPHA_SAT code (A),
  which were not tried.

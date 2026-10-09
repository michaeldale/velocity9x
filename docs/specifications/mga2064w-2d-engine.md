# MGA-2064W drawing engine: fill and screen copy

Reconciled 2026-10-09 for the matrox family's Phase 2
([plan](../plans/matrox-millennium-family.md)). The fill, copy, edge,
overlap and setup behaviour below was measured on A8U4I5's 2064W the same
day ([record](../decisions/2026-10-09-mga2064w-drawing-engine.md)); the rest
is the documents' word.

## Sources

- **MGA-1064SG Developer Specification** (Matrox, 1997),
  `C:\everything\L10GL\docs\datasheets\MGA-1064sg_199702.pdf`. The 1064SG
  "has the same Windows acceleration core as the MGA-2064W" (p.1-2). Page
  numbers below are the document's own (`p.4-49`). No 2064W databook
  (10470-MS-0300) is on this host.
- **FreeBE/AF Matrox driver**, `C:\everything\freebs12\freebe\matrox\driver.c`,
  which supports the Millennium (`MATROX_MILL_ID`).
- Marked **[mem]**: recollection of xf86-video-mga or matroxfb, unverified.
- `L10GL\...\mga1064.h` is wrong in several encodings (atype BLK, ZI/I,
  ILOAD variants, bltmod and pattern/transc positions, sdxr). Do not copy
  from it.

## Apertures

BAR0 (config 10h) is MGABASE1, the 16 KiB control aperture; BAR1 (14h) is
MGABASE2, the framebuffer - the reverse of the Millennium II. Drawing
registers are at 1C00h-1CFFh, all write-only, and their reads are not
decoded (Table 3-4). 1D00h-1DFFh mirrors them, and a write there starts the
engine; "the last register you program must be accessed in the
1D00h-1DFFh range" (sections 5.5.4-5.5.6). This driver's last write is
`YDSTLEN + 100h`.

## Registers used

| Reg | Offset | Fields | Page |
|---|---|---|---|
| DWGCTL | 1C00 | opcod 3:0, atype 6:4, linear 7, zmode 10:8, solid 11, arzero 12, sgnzero 13, shftzero 14, bop 19:16, trans 23:20, bltmod 28:25, pattern 29, transc 30 | 4-49..4-55 |
| MACCESS | 1C04 | pwidth 1:0 (00 8, 01 16, 10 32, 11 24), memreset 15 (keep 0), nodither 30, dit555 31 | 4-64 |
| PLNWT | 1C1C | plane mask, replicated per byte at 8/16 bpp | 4-69 |
| FCOL | 1C24 | foreground, replicated at 8/16 bpp | 4-56 |
| SGN | 1C58 | scanleft 0, sdxl 1, sdy 2, sdxr 5 | 4-71 |
| AR0 | 1C60 | BITBLT: source address of the far end of the first line, pixels; documented 18 bits | 4-20 |
| AR3 | 1C6C | BITBLT: source start, pixels, 24 bits; linear, YDSTORG not added | 4-23 |
| AR5 | 1C74 | BITBLT: source pitch in pixels, 18-bit signed, negative bottom-up | 4-25 |
| CXBNDRY | 1C80 | cxleft 10:0, cxright 26:16, inclusive; clipping cannot be disabled | 4-28 |
| FXBNDRY | 1C84 | fxleft 15:0, fxright 31:16 | 4-58 |
| YDSTLEN | 1C88 | length 15:0, yval 31:16 (xy addressing) | 4-81 |
| PITCH | 1C8C | iy 11:0 pixels, multiple of 32, at most 2048; ylin 15 | 4-68 |
| YDSTORG | 1C94 | pixel origin of the destination, 23 bits | 4-82 |
| YTOP / YBOT | 1C98 / 1C9C | linear `line * PITCH + YDSTORG`, multiples of 32, inclusive | 4-83, 4-79 |
| FIFOSTATUS | 1E10 | fifocount 5:0 free of 32, bfull 8, bempty 9 | 4-57 |
| STATUS | 1E14 | dwgengsts 16: busy until FIFO empty, command done, memory idle | 4-74 |

**Linearizer pitches** with ylin = 0 (p.4-68): 512, 640, 768, 800, 832, 960,
1024, 1152, 1280, 1600, 1664, 1920, 2048 pixels. FreeBE's Millennium list
omits 512, 832 and 1664; the builder does too.

**Alignment** (p.4-68, 4-82): PITCH and YDSTORG multiples of 64 pixels at
PW8, 32 at PW16, 64 at PW24, 32 at PW32. Stated as a block-mode restriction
with "must"; the builder obeys it for every atype. It is why the 2064W BIOS
pads 800-wide modes: 800 is not a multiple of 64 at 8 bpp.

## Per-mode setup

Section 5.5.3 (p.5-27): PITCH, YDSTORG, MACCESS, CXBNDRY, YTOP, YBOT, PLNWT.
The builder writes MACCESS pwidth, PLNWT all ones, CXBNDRY `07FF0000h`,
YTOP 0 and YBOT `007FFFE0h` once per mode, and PITCH and YDSTORG per
operation for that operation's destination. FreeBE writes MACCESS after
every 4F02h; nothing guarantees the BIOS left it matching.

## Fill

`DWGCTL = 000C7804h`: TRAP, atype RPL, solid, arzero, sgnzero, shftzero,
bop C (section 5.5.5.2, p.5-35). BLK (`000C7844h`) is not used: on the
1064SG it is an SGRAM block write needing OPTION hardpwmsk (p.4-50), and
the 2064W has WRAM and a different OPTION. FreeBE and [mem] xf86 use BLK on
the 2064W, so it is a candidate for later, after a readback test.

    DWGCTL, PITCH, YDSTORG, FCOL
    FXBNDRY         = ((x + w) << 16) | x       right edge exclusive
    YDSTLEN + 100h  = (y << 16) | h             y relative to YDSTORG

The exclusive right edge is p.5-33: "the bottom and right edges exist just
beyond the object's extents".

## Copy

`DWGCTL = 040C4008h`: BITBLT, bltmod BFCOL, shftzero, atype RPL, bop C,
sgnzero clear so SGN is honoured (section 5.5.6.2, p.5-41; with sgnzero set
it is p.5-58's `040C6008h`). DWGCTL goes before SGN: writing SGN while
sgnzero is set is "unpredictable" (p.4-71).

With P the source pitch in pixels and source addresses linear from the start
of VRAM:

    up    = same surface and dy > sy         -> sy, dy += h - 1; AR5 = -P; sdy
    left  = same surface, dy == sy, dx > sx  -> scanleft
    base  = source_offset / bpp + sy * P + sx
    AR3   = left ? base + w - 1 : base       (scan start)
    AR0   = left ? base : base + w - 1       (far end of the first line)
    FXBNDRY        = ((dx + w - 1) << 16) | dx   right edge inclusive
    YDSTLEN + 100h = (dy << 16) | h

FreeBE and [mem] xf86 both write `x + w - 1` for a blit and `x + w` for a
fill. Two surfaces whose bytes overlap are declined.

## Idle and the read cache

Wait for STATUS bit 16 clear before each operation, which also empties the
32-entry FIFO, and before the CPU touches the framebuffer. The engine's
writes do not invalidate the chip's 4-dword CPU read cache; any VGA
register write does (section 5.1.6). The HAL writes the CRTC index back to
itself after every successful blocking wait.

## Measured on the card (2026-10-09)

1. AR0 takes a full linear address well beyond its documented 18 bits.
2. The fill right edge is exclusive and the blit right edge inclusive.
3. The BIOS does not leave the engine usable after 4F02h: without the setup
   writes every case drew nothing. Which register was wrong is not known.
4. AR5 is written as a sign-extended dword; the 18-bit masked form drew a
   bottom-up copy wrong in 86Box and was never run on the card.

## Open questions

1. BLK fills on WRAM, and whether interleave doubles the alignment ([mem]
   xf86 shifts the YDSTORG modulo when interleaved; OPTION read
   `interleave=0` on the BringupKit card).
2. Whether ylin = 1 linear destinations work, which would let the engine
   draw into surfaces whose pitch the linearizer has no entry for.

# Trio64 fills and copies at 32 bpp: S3's two-word colour and mask, measured on 86Box only

2026-10-18, the 86Box Trio64 guest (`[PCI] S3 Trio64`, `5333:8811`, 4 MiB,
host port 9871), boots 367 and 368, driver `c83b15bd-dirty` (this change).
Phase 1, Trio64 row, of
[`../plans/32bpp-acceleration-and-3d.md`](../plans/32bpp-acceleration-and-3d.md).
Evidence:
[`../probe/86box-trio64-32bpp-blits-2026-10-18/`](../probe/86box-trio64-32bpp-blits-2026-10-18/).
**No physical Trio64 was run.**

## Sources: S3's own drivers

From the STB Jaz images (`C:\everything\stb-jaz-images\s3\`, recovered
2026-09-05 and outside git). Paths are relative to that tree:

- **The engine counts in 4-byte pixels from CR50 bits 5:4**, which the BIOS
  mode set programs to 32 bpp. Trio64V+ BIOS
  `bios\trio64vplus-97apr-SRC20502\CHIP\765\CHIPUTIL.ASM:842-857` shows it;
  no plain Trio64 or Trio32 BIOS source exists there. The driver does not
  set CR50.
- **The colour and write-mask registers are 16 bits wide** and take a
  32-bit value as two writes to the same port, low word then high.
  MULT_MISC bit 4 (RSF) selects the half and toggles after each write.
  S3's DirectDraw code writes MULT_MISC `E000h` to reset it, then the
  colour's two words
  (`win95\display-97apr-SRC31102\MINI\S3\S3_DD32.C:1160-1185`).
- **S3's shipped Win95 build pairs 16-bit writes** (`/DPLANES32`,
  `S3\MAKEFILE:14-20`) rather than setting MULT_MISC bit 9 for 32-bit
  access. SPR 9499 records 32-bit access dropping the mask's high word on
  764/732 boards (`win31\triov2-97apr-SOS30308\SOURCE\S3911.ASM:2353-2358`).
- S3's Win3.1, Win95 and NT drivers all accelerate 32 bpp blits on the
  Trio64. The limits recorded are on the Trio32 (732): no 32 bpp at
  800x600 and above (`win95\...\S3\VGA.ASM:2935-2940`). None is recorded
  for the 764.

## The change

`eng_s3_trio.c` declined every blit above 16 bpp. It now serves 32 bpp.
Before each 32 bpp operation it writes MULT_MISC `E000h`, which resets RSF
and leaves no external clip, no colour compare and both bases in the first
MByte, and then the write mask's two words as `FFFFh`. The 32-bit path had
never set the mask: at 8 and 16 bpp its low word, all ones from power-on
or the GDI path, is all there is. A fill then writes its colour's low and
high words. Packed 24 bpp is still declined, as S3's drivers exclude it.
The 8 and 16 bpp sequences are unchanged.

## The instrument

V9XDDF's off-screen surface has its own 256-byte pitch, which this engine
cannot address, so on the Trio64 every V9XDDF blit had gone to the CPU.
`/primary` (new) runs the same steps in the primary surface's top-left
64x16:
- a fill of the area;
- a rectangle fill;
- a non-overlapping copy along the same rows.

Every pixel is compared against a CPU reference. The desktop depth is set
with `V9XMSW /set:800x600xN`. Boot 368 was a fresh boot, so the counters
start at zero.

## Measured

| Boot | Driver | Mode | Fill | Rect fill + copy | Blits on the engine |
|---|---|---|---|---|---|
| 367 | b9a1d7c6 (before) | 800x600x32, pitch C80h | `0012A5C3`, 0 mismatches | `00ED5A3C`, 0 | CPU: that build declines above 16 bpp (counters cumulative, not separable) |
| 368 | this change | 800x600x32 | `0012A5C3`, 0 | `00ED5A3C`, 0 | 3 of 3 |
| 368 | same | 800x600x16 | `07E0`, 0 | `F81F`, 0 | 6 of 6 (cumulative) |
| 368 | same | 800x600x8 | `5A`, 0 | `A5`, 0 | 9 of 9 (cumulative) |

`EngineIdleTimeouts=0` throughout. The colours differ in every byte, so a
lost high word in the colour or the mask would have shown in the 32 bpp
fill.

## What this does and does not establish

- **86Box's Trio64 model fills and copies at 32 bpp with this sequence**,
  and S3's own drivers use the same sequence on silicon. That is two
  sources agreeing, not a measurement of the chip.
- **Not measured on a physical Trio64.** The 486's Trio64 VLB card is the
  machine for that, and the plan said to keep the decline if it disagrees.
  It is enabled now, by default, on 86Box's evidence and S3's source. If the
  486 disagrees, the decline goes back.
- Whether 86Box models the mask's high word at all, i.e. whether the mask
  writes are needed, is not tested here.
- GDI on the Trio64 still declines 32 bpp (`gdi_accel.c` gate 4).

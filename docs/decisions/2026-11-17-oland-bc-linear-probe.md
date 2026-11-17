# Oland: a linear-aligned BC1 texture does not sample by addrlib's rule

Date: 2026-11-17

Status: measured on W9QI0DAD (Oland XT `1002:6610`, FX-6300, Win98 SE)
at boot 61 with V9XSICP, outside the HAL. This is the probe that step 7
of [ati-gcn-ps11.md](../plans/ati-gcn-ps11.md) asked for before putting
DXT in a T#. The rule it tested is dead. The rule the sampler does
follow is not known.

Evidence: [`../probe/oland-dxt-w9qi0dad-2026-11-17/`](../probe/oland-dxt-w9qi0dad-2026-11-17/)
(`sicp-bc-boot61`, `sicp-bcmap-dwords`).

## The hypothesis

addrlib's linear-aligned rule (`EgBasedLib::ComputeSurfaceAlignmentsLinear`):
- rows aligned to max(8 elements, 64 bytes), a BC block being one
  element, so 64 bytes for BC1 and 128 for BC2/3;
- block (bx, by) at `by * pitch + bx * 8` for BC1.

The T# was radv's for GFX6, with each field checked against Mesa's
`registers/gfx6.json`:
- DATA_FORMAT BC1 (35);
- WIDTH and HEIGHT in texels;
- PITCH in texels (blocks x 4);
- TILING_INDEX 8 (linear aligned, the index every texture here uses).

## What the probe draws

`V9XSICP /ps11:6` (Bc1) draws `tex t0; mov r0, t0` from an 8x8 BC1
texture of 2x2 blocks laid out by the hypothesis.
- Each block has its own exact colour pair, so no interpolation is
  involved.
- 1,504 of 1,572 judged pixels were wrong.
- One colour, FF5200A5h, is RGB565 5014h expanded, which is the low half
  of block 0's index word. So index bits were read as colours.

`/ps11:9` (BcMap) reads the address directly.
- It fills 8 KB with words that each name themselves: every 32-bit word
  k holds 8000h | k as both BC1 colours (3-colour mode, so indices 0-2
  draw that colour).
- Every pixel is dumped (`[BcMap] Ryy`). A drawn texel says which word
  its colours came from; a transparent one had index 3.

16x16 texture, 4x4 blocks (`/bcw16`), the dominant word per texel,
shown as a byte offset:

- Block columns 0, 1, 2, 3 take their colours from offsets 0, 4, 16, 20
  (+ the row's base). That is 4-byte steps, interleaved: bit 0 of the
  column gives 4 bytes, bit 1 gives 16. Under the hypothesis it would be
  0, 8, 16, 24.
- Block rows 0, 1, 2, 3 are at 0, 1024, 2048 and 3072. Under the
  hypothesis that is 0, 64, 128, 192.
- In each block, the first texel row draws the colour. Rows 1-3 are
  mostly transparent, whatever the block. So the index half of a block
  is not the word after its colour half.
- An 8x8 texture reads its second block row at 2048. That is the 16x16's
  third row, as if the sampler took the 8x8 as 16 high.
- PITCH 64 instead of 32 (`/bcp64`) changed nothing.

The files in `sicp-bc-boot61` named `ps11-9-*` used an earlier marker:
8-byte blocks, dword 0 naming the block (8000h | n) and dword 1 naming
it differently (4000h | n). They show the same offsets, read as blocks;
`/bcp64` was run only with that marker. The word-numbered runs are in
`sicp-bcmap-dwords`.

## Hypotheses killed

- **addrlib's linear-aligned layout for BC1**, with the T# as radv
  writes it: wrong in pitch, in the horizontal step, and in where a
  block's index half is.
- **A wrong DATA_FORMAT or misplaced T# field**: BC1 is 35 in GFX6's
  enum, and every field position matches `gfx6.json`. The colours decode
  as BC1 does (565 endpoints, 3-colour transparency).
- **The probe's own writes**: one VRAM_DATA op wrote all 8 KB in order,
  as shader code is written. The 8888 textures the same probe draws
  read back by its pitch.

## Not known

- The rule the sampler follows. The 4-byte interleave and a pitch the
  T# does not set look more like a tiled arrangement than a linear one,
  but no tiled hypothesis was written down and tested.
- Whether BC2 and BC3 behave the same (`/ps11:7`, `/ps11:8` exist and
  were not run).
- Whether another TILING_INDEX gives the linear rule.

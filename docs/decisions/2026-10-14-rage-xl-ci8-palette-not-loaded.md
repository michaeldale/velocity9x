# The Rage XL samples CI8 textures, but no palette load tried so far reaches the colour

Date: 2026-10-14

Status: Open; four runs on A8U4I5 (ATI 3D Rage XL PCI, CONFIG_CHIP_ID
27004752, boot 470). Step 2 of
[rage-pro-table-fog-and-paletted-textures.md](../plans/rage-pro-table-fog-and-paletted-textures.md).
Evidence: `docs/probe/a8u4i5-rage-xl-pal8-2026-10-14/`. No driver code
changed.

## The probe

`ATIRX.EXE /pro /pal8` and `/palread`. ATIRX.VXD now takes the PCI id it
should map, so the Rage IIC scene tool can map the Rage XL. Each scene
draws one textured triangle into the 64x64 block at VRAM 2 MiB through
the HAL's own builders (`mach64_draw.c`), REPLACE, from an 8x8 texture.
The texture's state is built as RGB565, then DP_PIX_WIDTH's
SCALE_PIX_WIDTH is set to 2, the CI8 code in the XL/XC register reference.
Texel columns 0-3 hold index 3 and columns 4-7 hold index 200.

## What held

- **CI8 is sampled.** Every run drew two colours, split exactly at texel
  column 4: index 3 drew `83E0` and index 200 drew `000F`.
- **Those two colours never changed.** Five runs, sixteen scenes, and
  every load method gave the same two values.
- **No hang.** No FIFO timeout, idle timeout or reset in any run.

## Hypotheses the runs killed

| Run | Load | Result |
|---|---|---|
| 1 | `TEX_PALETTE_INDEX` (MM 0_D0, byte 0x740) = 0, then 256 writes to `TEX_PALETTE` (MM 0_DF, 0x77C), auto-incrementing | unchanged |
| 1 | Index then data, per entry; and the index in bits 15:8 | unchanged |
| 2 | One direct write to entry 3, then read back through the index register | unchanged (`83E0`) |
| 3 | ATI's format: `TEX_PALETTE` = index << 24 \| RGB888, through the FIFO, and directly with the engine idle | unchanged |
| 4 | As run 3, written after the CI8 state, with the state sent again for its `TEX_CACHE_FLUSH` | unchanged |
| 5 | The DAC route ATI's DLL takes for older chips: all 256 entries through `DAC_W_INDEX` and `DAC_DATA`, into the second palette, then into the primary (saved and restored) | unchanged |

ATI's format comes from its own ATI3DCIF.DLL (driver 4.11.2474,
`C:\ATI\SUPPORT\w82474en\ATI3DCIF.DL_` on A8U4I5, disassembled with
`wdis`). For chips at or above its internal class 0x203, it writes each
entry as `(index << 24) | (colour & 0xFFFFFF)` to byte 0x77C. Older chips
take the palette through the DAC instead: `DAC_W_INDEX` then three
`DAC_DATA` bytes. Afterwards it polls GUI_STAT bit 0. It never writes
0x740 for the palette. 0x740 is the first of twelve registers it fills
from float conversions per draw, which points at scaler setup rather
than a palette index. Whether the Rage XL is in ATI's ≥ 0x203 class was
not established.

`/palread` read `TEX_PALETTE` after writing each index to 0x740. Reading
back gives a table that is not the entries written: entry 3 reads `83E0`
and entries 20-3F a ramp; rows 50-6F repeat rows 20-3F. Both DAC palettes were
read too. The primary holds the identity ramp. The secondary, selected
by DAC_CNTL bit 5, is all zero. DAC_CNTL (`8601010A`) was restored.

## What is not known

- Where `83E0` and `000F` come from. Neither is anything the probe wrote.
- Whether the Rage XL has a writable texture palette at all. Forum users
  report that ATI's later XL drivers stopped listing PAL8. The XL/XC
  register reference omits the palette registers.

## Next, if this is resumed

1. Find the class test in ATI3DCIF.DLL (`cmp [edx],0x203`), and whether the
   XL passes it.
2. Or trace ATI's own driver loading a palette. That needs ATI's driver
   installed on A8U4I5.

The fallback needs none of this: advertise PAL8 and expand textures to
16-bit on the CPU through their palette.

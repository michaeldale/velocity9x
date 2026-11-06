# The VBE family leaves the DAC at 6 bits in 256-colour modes

Date: 2026-11-06. Feature gap, found in a feature review. Status: open,
nothing written, nothing measured.

## Gap

No module calls VBE 4F08h. In 8-bpp modes on the VBE family the palette is
written at 6 bits per channel, so 256-colour pictures and fades have 64
levels per channel instead of 256.

## Feature

When 4F00h's capabilities report a switchable DAC (bit 0), set 8 bits per
channel with 4F08h after each 8-bpp mode set, read back the width the BIOS
granted, and write the palette at that width. Keep 6 bits when the call
fails or is not offered.

## Acceptance

On one guest whose BIOS reports a switchable DAC, the boot record shows
the granted width, and a 256-step grey ramp at 8 bpp shows 256 distinct
levels in a capture.

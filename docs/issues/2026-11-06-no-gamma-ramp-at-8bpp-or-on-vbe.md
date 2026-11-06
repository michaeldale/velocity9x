# No gamma ramp at 8 bpp, and none on the VBE family

Date: 2026-11-06. Feature gap, found in a feature review. Status: open,
nothing written, nothing measured.

## Gap

`DdiGammaRamp` (`src/display16/ddi.c`) answers only when
`v9x_gamma_available()`: a family with a `load_gamma` hook and a mode that
is not palettized. So:

- at 8 bpp no family accepts `SetDeviceGammaRamp`, and DirectDraw does not
  get `DDCAPS2_PRIMARYGAMMA`; full-screen 8-bpp games' brightness sliders
  do nothing;
- the VBE family has no `load_gamma` hook, so it has no gamma at any depth.

## Feature

1. At 8 bpp, on every family: apply the ramp to the palette. Keep the
   application's logical palette, and write ramp(palette) to the DAC on
   every palette set and every ramp set, per channel. Publish
   `DDCAPS2_PRIMARYGAMMA` in 8-bpp modes.
2. On the VBE family above 8 bpp: only where the DAC is known to be a
   gamma table for direct colour (VBE 4F09h on a BIOS that allows it, or a
   chip hook); otherwise keep returning FALSE, as now.

The ramp-times-palette arithmetic belongs in host-tested C.

## Acceptance

At 8 bpp on one guest, `SetDeviceGammaRamp` returns TRUE and a read of the
DAC shows the ramped palette; a game's brightness slider changes the
picture.

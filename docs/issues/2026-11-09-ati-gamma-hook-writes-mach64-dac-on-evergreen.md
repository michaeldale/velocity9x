# The ati family's gamma hook writes Mach64 DAC offsets into Evergreen registers

Date: 2026-11-09. Status: open. Found by reading while reviewing the
HD 5570's first run
([GitHub issue 16](https://github.com/michaeldale/velocity9x/issues/16)).
Not reproduced, not measured on any card.

## What the code does

The `ati` family has one `V9X_HW16_OPS` table (`src/chipsets/ati/ati_hw16.c`,
`v9x_hw16`) for every row it claims, Mach64 to GCN. Its gamma hook is
`v9x_ati_load_gamma` (same file). It writes the Mach64/Rage Pro DAC:
`DAC_REGS` at byte `04C0h`, `DAC_DATA` at `04C1h`, `DAC_CNTL` at `04C4h`
of the register window it is handed. It was measured on the Rage XL
(`1a9ae3ec`, Thief II).

`ddi.c` decides gamma is available when the family has a hook and the
active device has a `fill_engine_descriptor` (`v9x_gamma_hardware`), and
the mode is not palettized. It then maps the window that descriptor names
and calls the hook with it (`v9x_program_gamma`).

The Evergreen rows (Cedar, Juniper, Redwood, Caicos, Trinity in
`src/chipsets/ati/evergreen/cedar_hw16.c`) all have a descriptor:
`v9x_evergreen_fill_engine` names BAR2's register window. So on every
Evergreen card at 16 or 32 bpp:

- `SetDeviceGammaRamp` is accepted and returns success;
- the hook reads byte `04C4h`, may write it, then writes 768 bytes to
  byte `04C1h` of Evergreen's register space;
- the display controller's gamma table is never written. On Evergreen
  that table is `DC_LUT_RW_MODE` `69E0h`, `DC_LUT_RW_INDEX` `69E4h`,
  `DC_LUT_30_COLOR` `69F0h` and `DC_LUT_WRITE_EN_MASK` `69F8h`, per
  CRTC (`build\external\evergreen\linux-radeon\evergreen_reg.h`).

What Evergreen has at `04C0h`-`04C4h` was not found: the register is in
neither `evergreend.h` nor `evergreen_reg.h`.

## Who else takes the same path

The gate is the descriptor, so every `ati` row with one: Rage 128
(`rage128_hw16.c`), RS690 (`rs690_hw16.c`), Carrizo (`carrizo_hw16.c`)
and Mullins (`mullins_hw16.c`) as well. Whether `04C0h` is a palette
register on any of them was not checked. The Mach64-class rows
(`mobility_hw16.c`, `rage_iic_hw16.c`) are the ones the hook was written
for.

## What it could explain

- Games that brighten through the hardware ramp (Quake 3, Half-Life,
  Unreal, games with a brightness slider) would show no change on these
  cards, while the game believes the ramp took.
- It does **not** explain the dark Quake 1 and Quake 2 in issue 16: those
  apply gamma to their textures and do not call the ramp.
- A write of 768 bytes to an unknown register is a risk in its own right.
  Nothing has been seen to go wrong from it.

## Check

1. On an Evergreen machine (M-D7TFW4 Trinity or M-BXMFA8 Caicos) at
   16 bpp: run Quake 3 with `r_gamma 2`, read `V9XESC.INI`'s
   `[Velocity9xGamma]` (the sets and writes are logged) and look at the
   picture. Expected if this record is right: writes counted, picture
   unchanged.
2. Read `04C0h`-`04C4h` before and after with the register probe
   (V9XEGMM), to see what the writes land on.

## Fix, when confirmed

Per chip, not in the shared hook: Evergreen's rows load DC_LUT for the
CRTC that scans the desktop (`eg_scanout.c` already identifies D1);
rows whose palette is not known stop claiming gamma by setting a flag
the gate reads, so `SetDeviceGammaRamp` returns FALSE there, as the
VBE family does. The table logic stays in host-testable C; the hook only
writes registers.

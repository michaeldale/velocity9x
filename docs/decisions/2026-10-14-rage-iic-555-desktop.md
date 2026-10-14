# The Rage IIC's 15 bpp modes were dropped at ring 0: a 555 desktop needs them kept

Date: 2026-10-14

Status: Measured on A8U4I5 (Rage IIC AGP), boots 457 to 460. Phase 0,
step 1 of [ati-555-desktop.md](../plans/ati-555-desktop.md). Evidence:
`docs/probe/a8u4i5-rage-iic-555-2026-10-14/`.

## What was tried

`[Velocity9x] HighColor=15` in SYSTEM.INI forces the 5:5:5 layout
whatever the engine. Under it the driver sets the 15 bpp sibling of the
16 bpp row, 0x116 for 1024x768.

## Boot 457: refused

- **Driver:** `Stage=fail-hardware-aperture`, `VbeDetail=minivdd-no-mode`.
  The tier-0 aperture check found no mini-VDD record for 0x116. Windows
  then came up on its 16-colour fallback with `BitsPerPixel=4`, and boot
  459 needed the 16 bpp registry restored.
- **Cause:** the mini-VDD's ring-0 mode filter (`loader.asm`,
  `V9xMini_Vbe_Query_Record`) kept BitsPerPixel 8, 16, 24 and 32 only. An
  ATI BIOS reports its 5:5:5 modes as **15** bits per pixel. The Rage XL
  AGP's did in marxveix's survey: 0x110, 0x113 and 0x116 at
  `Bpp=15`, attributes `BB`, masks 5:5:5
  (`docs/probe/rage-xl-agp-marxveix-2026-10-03/V9XSURV.INI`). So the
  Rage IIC's cache held 28 of the BIOS's 40 modes, and none at 15 bpp.
- **Why the ViRGE never hit this:** its BIOS reports its 555 modes as
  16 bpp, so they passed the filter (inference from the ViRGE 555
  desktop having worked).

## The fix

- **The mini-VDD** keeps 15 bpp records.
- **The tier-0 check** (`enable16.c`) reads the BIOS's depth through
  `v9x_vbe_storage_bits`: 15 is two bytes a pixel, like the 16 bpp row it
  replaces. The test was written first and failed.
- **Scanned-row admission** still refuses 15 bpp, so no mode table gains
  rows. The only use of the new records is the 555 sibling's aperture
  and stride.

## Boot 460: a 5:5:5 desktop

- **Driver:** `Stage=enable-ok`, `Colour=hc=15 ini=15 row=279 set=278
  is555=1 mask=31744`, `VbeDetail=ok`, layout `555-ini`, 1024x768.
- **BIOS:** the Rage IIC reports nine 15 bpp modes: 0x110, 0x113, 0x116,
  0x119, 0x10D, and ATI's own 0x213, 0x223, 0x233 and 0x243. All are
  linear at `DC000000` with 5:5:5 masks (`rgb=0a05,0505,0005`). The cache
  now holds 37 of 40.
- **Desktop:** correct colours in the agent's screenshot
  (`boot460-desktop-555.png`). The desktop does not flip, so the GDI
  capture shows it.

Direct3D on this desktop is not yet drawn. The Rage II policy refuses
any target but 565 (plan, Phase 1), and so does ATI3DCIF.DLL's path to
the engine.

Gates: build-host (the new test failed before the helper, and passes
after); run-checks.
